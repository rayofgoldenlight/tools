-- src/app.lua
local Model  = require("src.model")
local Crypto = require("src.crypto.crypto")
local Basic  = require("src.ui.basic")

local App = {}

local function inside(mx, my, r)
  return mx >= r.x and mx <= r.x + r.w and my >= r.y and my <= r.y + r.h
end

local function wrapText(text, maxWidth)
  return text or ""
end

local function drawTooltip(text, mx, my)
  if not text or text == "" then return end
  local font = love.graphics.getFont()
  local pad = 10
  local maxW = 420

  -- measure height by rough line wrapping using printf
  -- compute lines by splitting on \n and just rely on printf wrapping visually.
  -- (Good enough for now.)
  local w = math.min(maxW, font:getWidth(text) + pad*2)
  w = math.max(200, w)

  local x = mx + 16
  local y = my + 16
  local screenW, screenH = love.graphics.getWidth(), love.graphics.getHeight()
  local h = 80 -- default; will expand visually via printf; just allocate decent space
  h = 140

  if x + w > screenW - 10 then x = screenW - w - 10 end
  if y + h > screenH - 10 then y = screenH - h - 10 end

  love.graphics.setColor(0, 0, 0, 0.85)
  love.graphics.rectangle("fill", x, y, w, h, 8, 8)
  love.graphics.setColor(1, 1, 1, 0.9)
  love.graphics.rectangle("line", x, y, w, h, 8, 8)

  love.graphics.setColor(1,1,1)
  love.graphics.printf(text, x + pad, y + pad, w - pad*2)
end

local function computeGridLayout(items, opts)
  -- opts: { yStart, margin, cardW, cardH, gap }
  local rects = {}
  local W = love.graphics.getWidth()

  local yStart = opts.yStart or 110
  local margin = opts.margin or 30
  local cardW  = opts.cardW or 260
  local cardH  = opts.cardH or 120
  local gap    = opts.gap or 18

  local cols = math.max(1, math.floor((W - margin*2 + gap) / (cardW + gap)))

  local maxRow = 0
  for i, item in ipairs(items) do
    local col = (i-1) % cols
    local row = math.floor((i-1) / cols)
    if row > maxRow then maxRow = row end

    rects[item.id] = {
      x = margin + col * (cardW + gap),
      y = yStart + row * (cardH + gap),
      w = cardW,
      h = cardH
    }
  end

  local contentH = yStart + (maxRow + 1) * (cardH + gap)
  return rects, contentH
end

-- ---- Grid layout caching ----

local function gridOptsKey(opts)
  -- fields that affect layout.
  local yStart = opts.yStart or 110
  local margin = opts.margin or 30
  local cardW  = opts.cardW  or 260
  local cardH  = opts.cardH  or 120
  local gap    = opts.gap    or 18
  return table.concat({yStart, margin, cardW, cardH, gap}, "|")
end

-- cache is a table you keep per-view (tasks/subtasks/mergePreview/mergeSubtasks)
-- listKey identifies "which list" is being laid out (e.g. taskId for subtasks view)
local function getGridLayoutCached(cache, items, opts, listKey)
  items = items or {}
  opts = opts or {}

  local W = love.graphics.getWidth()
  local n = #items
  local oKey = gridOptsKey(opts)

  local valid =
    cache.rects ~= nil and
    cache.contentH ~= nil and
    cache.W == W and
    cache.n == n and
    cache.optsKey == oKey and
    cache.listKey == listKey

  if not valid then
    cache.W = W
    cache.n = n
    cache.optsKey = oKey
    cache.listKey = listKey
    cache.rects, cache.contentH = computeGridLayout(items, opts)
  end

  return cache.rects, cache.contentH
end


function App.wheelmoved(dx, dy)
  if App.modal then return end

  local step = 40
  local delta = dy * step  -- dy > 0 means wheel up

  if App.state == "tasks" then
    App.scroll.tasks = App.scroll.tasks + delta
  elseif App.state == "subtasks" then
    App.scroll.subtasks = App.scroll.subtasks + delta
  elseif App.state == "mergePreview" then
    App.scroll.mergePreview = App.scroll.mergePreview + delta
  elseif App.state == "mergeSubtasks" then
    App.scroll.mergeSubtasks = App.scroll.mergeSubtasks + delta
  end
end

local function ctrlDown()
  return love.keyboard.isDown("lctrl") or love.keyboard.isDown("rctrl")
end

local function setNotice(text, seconds)
  App.notice = { text = text, untilTime = love.timer.getTime() + (seconds or 2.0) }
end

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function clampScroll(current, contentH, viewH, topPad)
  -- current: scrollY (negative means scrolled down)
  -- contentH: total content height in content coordinates
  -- viewH: screen height
  -- topPad: the y where content starts (header height)
  local available = viewH - topPad
  local maxDown = math.max(0, contentH - available) -- how far content exceeds view
  -- scroll range: 0 .. -maxDown
  return clamp(current, -maxDown, 0)
end

local function openConfirm(message, onYes, onNo)
  App.modal = { kind="confirm", message=message, onYes=onYes, onNo=onNo }
end

local function openPrompt(title, initial, onOk, onCancel, opts)
  App.modal = {
    kind="prompt",
    title=title,
    inputId="modal.input",
    onOk=onOk,
    onCancel=onCancel,
    opts=opts or {}
  }
  Basic.set("modal.input", initial or "")
  App.ui.active = "modal.input"
end

local function closeModal()
  App.modal = nil
end

local function ensureUsernameThen(actionFn)
  if App.session.username and App.session.username ~= "" then
    actionFn(App.session.username)
    return
  end

  -- remember what we wanted to do
  App.pending = actionFn
  openPrompt("Enter username", "", function(name)
    name = (name or ""):gsub("^%s+", ""):gsub("%s+$", "")
    if name == "" then
      setNotice("Username cannot be empty", 2)
      return
    end
    App.session.username = name
    closeModal()
    local f = App.pending
    App.pending = nil
    if f then f(name) end
  end, function()
    App.pending = nil
    closeModal()
  end)
end

local function generateDemoExport()
  -- Build a demo board
  local data = Model.newEmpty()
  local idx = Model.rebuildIndex(data)

  local t1 = Model.addTask(data, idx, "Demo Task A", "This is a demo description.", {"Sub 1", "Sub 2", "Sub 3"})
  local t2 = Model.addTask(data, idx, "Demo Task B", "Another description here.", {"Alpha", "Beta"})

  -- Assign some subtasks to show names in summary
  local session = Model.newSession()
  session.username = "Alice"
  Model.assignSubtask(data, idx, session, idx.taskById[t1].task.subtasks[1].id, "Alice", { override = true })

  session.username = "Bob"
  Model.assignSubtask(data, idx, session, idx.taskById[t1].task.subtasks[2].id, "Bob", { override = true })
  Model.assignSubtask(data, idx, session, idx.taskById[t2].task.subtasks[1].id, "Bob", { override = true })

  -- Export
  local password = "demo-password"
  local exported = Crypto.exportString(data, password)

  print("=== DEMO EXPORT ===")
  print("Password:", password)
  print("String:", exported)
  print("===================")

  love.system.setClipboardText(exported)

  -- Pre-fill login fields for convenience
  Basic.set("login.string", exported)
  Basic.set("login.password", password)
end

local function generateMergeTestExport()
  -- Start from the same base as demo so you can see diffs
  local data = Model.newEmpty()
  local idx = Model.rebuildIndex(data)

  local t1 = Model.addTask(data, idx, "Demo Task A (edited)", "CHANGED description here.", {"Sub 1", "Sub 2", "Sub 3"})
  local t2 = Model.addTask(data, idx, "Demo Task B", "Another description here.", {"Alpha", "Beta"})
  local t3 = Model.addTask(data, idx, "New Task C", "This task is NEW in incoming.", {"X", "Y"})

  -- assignment changes: move Sub 1 to Bob, clear Sub 2, etc.
  local session = Model.newSession()
  Model.assignSubtask(data, idx, session, idx.taskById[t1].task.subtasks[1].id, "Bob", { override = true })
  idx.taskById[t1].task.subtasks[2].assignee = nil -- removed assignment example
  Model.assignSubtask(data, idx, session, idx.taskById[t2].task.subtasks[2].id, "Alice", { override = true })

  local password = "merge-password"
  local exported = Crypto.exportString(data, password)

  print("=== MERGE TEST EXPORT (incoming) ===")
  print("Password:", password)
  print("String:", exported)
  print("===================================")

  love.system.setClipboardText(exported)
  Basic.set("merge.string", exported)
  Basic.set("merge.password", password)
end

local function startMerge()
  App.merge.err = nil
  Basic.set("merge.string", "")
  Basic.set("merge.password", "")
  App.prevState = App.state
  App.state = "mergeLogin"
  App.ui.active = "merge.string"
end

local function loadMergeDiff()
  App.merge.err = nil
  local b64 = Basic.get("merge.string")
  local pw  = Basic.get("merge.password")

  local incoming, err = Crypto.importString(b64, pw)
  if not incoming then
    App.merge.err = err
    return
  end

  incoming = Model.normalize(incoming)
  incoming = Model.repairCounters(incoming)
  local incomingIndex = Model.rebuildIndex(incoming)

  local diff = Model.computeMergeDiff(App.data, App.index, incoming, incomingIndex)

  diff._newSet = {}
  for _, id in ipairs(diff.newTasks or {}) do
    diff._newSet[id] = true
  end

  diff._delSet = {}
  for _, id in ipairs(diff.deletedTasks or {}) do
    diff._delSet[id] = true
  end

  diff._assignDelta = {}

  local function assigneeSet(task)
    local set = {}
    for _, sub in ipairs(task.subtasks or {}) do
      local a = sub.assignee
      if type(a) == "string" and a ~= "" then
        set[a] = true
      end
    end
    return set
  end

  local function setToSortedList(set)
    local out = {}
    for name, _ in pairs(set) do
      out[#out+1] = name
    end
    table.sort(out)
    return out
  end

  for taskId, baseEntry in pairs(App.index.taskById) do
    local incEntry = incomingIndex.taskById[taskId]
    if incEntry then
      local bSet = assigneeSet(baseEntry.task)
      local nSet = assigneeSet(incEntry.task)

      local addedSet, removedSet = {}, {}

      for name, _ in pairs(nSet) do
        if not bSet[name] then addedSet[name] = true end
      end
      for name, _ in pairs(bSet) do
        if not nSet[name] then removedSet[name] = true end
      end

      diff._assignDelta[taskId] = {
        added   = setToSortedList(addedSet),
        removed = setToSortedList(removedSet),
      }
    end
  end

  

  App.merge.incomingData = incoming
  App.merge.incomingIndex = incomingIndex
  App.merge.diff = diff
  App.merge.viewTaskId = nil

  App.scroll.mergePreview = 0
  App.scroll.mergeSubtasks = 0

  App.mergeVersion = (App.mergeVersion or 0) + 1

  App.state = "mergePreview"
end

local function applyMergeNow()
  local diff = App.merge.diff
  if not diff then return end

  Model.applyMerge(
    App.data, App.index,
    App.merge.incomingData, App.merge.incomingIndex,
    diff,
    {
      importEdits = App.merge.optEdits,
      importAssignments = App.merge.optAssignments,
      importNewTasks = App.merge.optNewTasks,
      importDeletions = App.merge.optDeletedTasks,
    }
  )

  -- keep counters sane
  App.data = Model.repairCounters(App.data)

  setNotice("Merge applied", 1.6)
  App.state = App.prevState or "tasks"
  App.prevState = nil
end

local function exportNow()
  if not App.data then
    setNotice("Nothing to export", 2)
    return
  end

  openPrompt("Export password", "", function(pw)
    pw = (pw or ""):gsub("^%s+", ""):gsub("%s+$", "")
    if pw == "" then
      setNotice("Password cannot be empty", 2)
      return
    end

    local ok, exportedOrErr = pcall(function()
      return Crypto.exportString(App.data, pw)
    end)

    if not ok then
      setNotice("Export failed: "..tostring(exportedOrErr), 3)
      return
    end

    love.system.setClipboardText(exportedOrErr)
    closeModal()

    -- open preview modal
    App.modal = {
      kind = "exportPreview",
      exported = exportedOrErr
    }
    setNotice("Export copied to clipboard", 2.5)
  end, function()
    closeModal()
  end, { password = true })
end

local function mergeTaskCardStatus(taskId)
  -- returns: "new" | "deleted" | "updated" | "same"
  local d = App.merge.diff
  if not d then return "same" end

  if d._newSet and d._newSet[taskId] then
    return "new"
  end
  if d._delSet and d._delSet[taskId] then
    return "deleted"
  end
  if d.updatedTasks and d.updatedTasks[taskId] then
    return "updated"
  end

  return "same"
end

local function assigneeSetFromTask(task)
  local set = {}
  for _, sub in ipairs(task.subtasks or {}) do
    local a = sub.assignee
    if type(a) == "string" and a ~= "" then set[a] = true end
  end
  return set
end

local function setToSortedList(set)
  local t = {}
  for k, _ in pairs(set) do t[#t+1] = k end
  table.sort(t)
  return t
end

-- returns: addedNames(list), removedNames(list)
local function assignmentDeltaForTask(taskId)
  local d = App.merge.diff
  if d and d._assignDelta and d._assignDelta[taskId] then
    local row = d._assignDelta[taskId]
    return row.added or {}, row.removed or {}
  end

  -- fallback (should rarely run now)
  local bEntry = App.index.taskById[taskId]
  local nEntry = App.merge.incomingIndex and App.merge.incomingIndex.taskById[taskId]
  if not bEntry or not nEntry then return {}, {} end

  local bSet = assigneeSetFromTask(bEntry.task)
  local nSet = assigneeSetFromTask(nEntry.task)

  local added, removed = {}, {}
  for name, _ in pairs(nSet) do
    if not bSet[name] then added[name] = true end
  end
  for name, _ in pairs(bSet) do
    if not nSet[name] then removed[name] = true end
  end
  return setToSortedList(added), setToSortedList(removed)
end

-- Choose a style based on assignment adds/removals + new/deleted/updated.
-- returns: bg(r,g,b), border(r,g,b), badgeText (string)
local function mergeTaskStyle(taskId)
  local status = mergeTaskCardStatus(taskId)

  if status == "new" then
    return {0.10, 0.20, 0.12}, {0.35, 1.0, 0.45}, "+NEW"
  end
  if status == "deleted" then
    return {0.22, 0.10, 0.10}, {1.0, 0.35, 0.35}, "-DEL"
  end

  local added, removed = assignmentDeltaForTask(taskId)
  local hasAdd = #added > 0
  local hasRem = #removed > 0

  if hasAdd and not hasRem then
    return {0.10, 0.18, 0.12}, {0.35, 1.0, 0.45}, ("+"..#added)
  elseif hasRem and not hasAdd then
    return {0.20, 0.10, 0.10}, {1.0, 0.35, 0.35}, ("-"..#removed)
  elseif hasAdd and hasRem then
    return {0.16, 0.12, 0.18}, {0.85, 0.55, 1.0}, ("+"..#added.." / -"..#removed)
  end

  if status == "updated" then
    return {0.14, 0.14, 0.20}, {0.55, 0.65, 1.0}, "EDIT"
  end

  return {0.14, 0.14, 0.16}, {0.35, 0.35, 0.35}, ""
end

function App.load()
  App.ui = Basic.new()
  Basic.bind(App.ui)

  App.state = "login"
  App.data = nil
  App.index = nil
  App.session = nil

  App.err = nil

  -- inputs
  Basic.set("login.string", "")
  Basic.set("login.password", "")
  App.ui.active = "login.string"

  App.lastClick = { time = 0, taskId = nil }
  App.selectedTaskId = nil
  App.viewTaskId = nil

  App.mode = "assign"     -- "normal" | "edit" | "delete"
  App.modal = nil         -- active modal (table) or nil
  App.notice = nil        -- {text=..., untilTime=...}
  App.pending = nil 

    App.prevState = nil

  App.merge = {
    incomingData = nil,
    incomingIndex = nil,
    diff = nil,

    -- options (checkboxes)
    optEdits = true,
    optAssignments = true,
    optNewTasks = true,
    optDeletedTasks = true,

    viewTaskId = nil, -- for mergeSubtasks
    err = nil,
  }

  Basic.set("merge.string", "")
  Basic.set("merge.password", "")

  App.scroll = {
    tasks = 0,
    subtasks = 0,
    mergePreview = 0,
    mergeSubtasks = 0
  }

  App.layoutCache = {
    tasks = {},
    subtasks = {},
    mergePreview = {},
    mergeSubtasks = {}
  }

  -- increments whenever a new merge diff is loaded (used as a listKey)
  App.mergeVersion = 0

end

local function importNow()
  App.err = nil
  local b64 = Basic.get("login.string")
  local pw  = Basic.get("login.password")

  local imported, err = Crypto.importString(b64, pw)
  if not imported then
    App.err = err
    return
  end

  imported = Model.normalize(imported)
  imported = Model.repairCounters(imported)
  local idx = Model.rebuildIndex(imported)

  App.data = imported
  App.index = idx
  App.session = Model.newSession()

  App.state = "tasks" -- stub for now
end

local function newFileNow()
  App.err = nil

  App.data = Model.newEmpty()
  App.data = Model.repairCounters(App.data)
  App.index = Model.rebuildIndex(App.data)
  App.session = Model.newSession()

  -- reset some UI state
  App.selectedTaskId = nil
  App.viewTaskId = nil
  App.mode = "assign"
  App.scroll.tasks = 0
  App.scroll.subtasks = 0

  App.state = "tasks"
end

function App.update(dt) end

function App.draw()
  love.graphics.clear(0.06, 0.06, 0.07)

  if App.state == "login" then
    love.graphics.setColor(1,1,1)
    love.graphics.print("Encrypted Task Manager", 40, 30)
    love.graphics.setColor(0.8,0.8,0.8)
    love.graphics.print("Paste encrypted string + enter password", 40, 55)

    local w = love.graphics.getWidth()

    Basic.textInput("login.string",   {x=40, y=110, w=w-80, h=44}, {label="Encrypted String (base64)"})
    Basic.textInput("login.password", {x=40, y=190, w=w-80, h=44}, {label="Password", password=true})

    local y = 260
    local btnW, btnH = 160, 44
    local gap = 20

    if Basic.button("login.import", {x=40, y=y, w=btnW, h=btnH}, "Import") then
      importNow()
    end

    if Basic.button("login.newFile", {x=40 + btnW + gap, y=y, w=btnW, h=btnH}, "New File") then
      newFileNow()
    end

    love.graphics.setColor(0.7,0.7,0.7)
    love.graphics.print("Enter = import | Tab = switch field | Ctrl+V = paste | F2 = demo export", 40, 320)

    if App.err then
      love.graphics.setColor(1, 0.35, 0.35)
      love.graphics.print("Error: "..tostring(App.err), 40, 360)
    end

  elseif App.state == "tasks" then
    love.graphics.setColor(1,1,1)
    love.graphics.print("Tasks", 40, 30)

    local exportBtn = {x=love.graphics.getWidth()-180, y=26, w=140, h=36}

    if Basic.button("tasks.export", exportBtn, "Export") then
      exportNow()
    end
    local modeText = "Mode: "..App.mode.."   (Ctrl+E edit, Ctrl+D delete, Ctrl+U username, Ctrl+M merge)"
    local font = love.graphics.getFont()
    local textW = font:getWidth(modeText)

    local modeX = exportBtn.x + exportBtn.w - textW   
    modeX = math.max(40, modeX)                
    local modeY = exportBtn.y + exportBtn.h + 6  
    
    love.graphics.setColor(0.7,0.7,0.7)
    love.graphics.print(modeText, modeX, modeY)

    modeText = "Double-click a task to view subtasks | Right-click a task to perform action"
    textW = font:getWidth(modeText)
    modeX = exportBtn.x + exportBtn.w - textW   
    modeX = math.max(40, modeX) 
    modeY = exportBtn.y + exportBtn.h + 22  

    love.graphics.print(modeText, modeX, modeY)

    local tasks = App.data.tasks or {}
    local rects, contentH = getGridLayoutCached(
      App.layoutCache.tasks,
      tasks,
      { yStart=110, margin=30, cardW=260, cardH=120, gap=18 },
      "tasks" -- listKey
    )

    App.scroll.tasks = clampScroll(App.scroll.tasks, contentH, love.graphics.getHeight(), 110)

    local mx, my = love.mouse.getPosition()
    local hoverTask = nil

    love.graphics.push()
    love.graphics.translate(0, App.scroll.tasks)

    for _, task in ipairs(tasks) do
      local r = rects[task.id]
      local hovered = inside(mx, my - App.scroll.tasks, r)
      if hovered then hoverTask = task end

      if App.selectedTaskId == task.id then
        love.graphics.setColor(0.20, 0.22, 0.30)
      else
        love.graphics.setColor(0.14, 0.14, 0.16)
      end
      love.graphics.rectangle("fill", r.x, r.y, r.w, r.h, 10, 10)

      love.graphics.setColor(hovered and 0.9 or 0.35, hovered and 0.9 or 0.35, hovered and 0.9 or 0.35)
      love.graphics.rectangle("line", r.x, r.y, r.w, r.h, 10, 10)

      love.graphics.setColor(1,1,1)
      love.graphics.printf(task.title, r.x + 12, r.y + 10, r.w - 24)

      local subCount = #(task.subtasks or {})
      local summary = Model.taskAssigneeSummary(task)
      love.graphics.setColor(0.75,0.75,0.75)
      love.graphics.print(("Subtasks: %d"):format(subCount), r.x + 12, r.y + r.h - 38)

      if summary ~= "" then
        love.graphics.setColor(0.70,0.85,0.70)
        love.graphics.printf("Assigned: "..summary, r.x + 12, r.y + r.h - 20, r.w - 24)
      else
        love.graphics.setColor(0.75,0.75,0.75)
        love.graphics.printf("Assigned: (none)", r.x + 12, r.y + r.h - 20, r.w - 24)
      end
    end

    love.graphics.pop()

    if hoverTask then
      drawTooltip(Model.taskHoverText(hoverTask), mx, my)
    end

  elseif App.state == "subtasks" then
    local task = App.index.taskById[App.viewTaskId] and App.index.taskById[App.viewTaskId].task

    love.graphics.setColor(1,1,1)
    love.graphics.print("Subtasks", 40, 30)

    local exportBtn = {x=love.graphics.getWidth()-180, y=26, w=140, h=36}

    if Basic.button("subtasks.export", exportBtn, "Export") then
      exportNow()
    end

    local modeText = "Mode: "..App.mode.."   (Ctrl+E edit, Ctrl+D delete, Ctrl+U username, Ctrl+M merge)"
    local font = love.graphics.getFont()
    local textW = font:getWidth(modeText)

    local modeX = exportBtn.x + exportBtn.w - textW   
    modeX = math.max(40, modeX)                
    local modeY = exportBtn.y + exportBtn.h + 6  
    
    love.graphics.setColor(0.7,0.7,0.7)
    love.graphics.print(modeText, modeX, modeY)

    if Basic.button("sub.back", {x=40, y=70, w=120, h=40}, "Back") then
      App.state = "tasks"
      App.viewTaskId = nil
    end

    if task then
      love.graphics.setColor(1,1,1)
      love.graphics.print(task.title, 180, 78)
      love.graphics.setColor(0.75,0.75,0.75)
      love.graphics.print("Assigned: "..(Model.taskAssigneeSummary(task) ~= "" and Model.taskAssigneeSummary(task) or "(none)"), 180, 98)

      local subs = task.subtasks or {}
      local rects, contentH = getGridLayoutCached(
        App.layoutCache.subtasks,
        subs,
        { yStart=140, margin=30, cardW=240, cardH=90, gap=14 },
        App.viewTaskId -- listKey
      )
      App.scroll.subtasks = clampScroll(App.scroll.subtasks, contentH, love.graphics.getHeight(), 140)

      local mx, my = love.mouse.getPosition()
      local hoverText = nil

      love.graphics.push()
      love.graphics.translate(0, App.scroll.subtasks)

      for _, sub in ipairs(subs) do
        local r = rects[sub.id]
        local hovered = inside(mx, my - App.scroll.subtasks, r)

        love.graphics.setColor(0.14,0.14,0.16)
        love.graphics.rectangle("fill", r.x, r.y, r.w, r.h, 10, 10)
        love.graphics.setColor(hovered and 0.9 or 0.35, hovered and 0.9 or 0.35, hovered and 0.9 or 0.35)
        love.graphics.rectangle("line", r.x, r.y, r.w, r.h, 10, 10)

        love.graphics.setColor(1,1,1)
        love.graphics.printf(sub.title, r.x + 12, r.y + 10, r.w - 24)

        local a = sub.assignee
        love.graphics.setColor(a and 0.70 or 0.75, a and 0.85 or 0.75, a and 0.70 or 0.75)
        love.graphics.printf("Assignee: "..(a or "(none)"), r.x + 12, r.y + r.h - 26, r.w - 24)

        if hovered then hoverText = "Assignee: "..(a or "(none)") end
      end

      love.graphics.pop()

      if hoverText then drawTooltip(hoverText, mx, my) end
    else
      love.graphics.setColor(1,0.35,0.35)
      love.graphics.print("Task not found.", 40, 130)
    end

  elseif App.state == "mergeLogin" then
    love.graphics.setColor(1,1,1)
    love.graphics.print("Merge (Ctrl+M)", 40, 30)
    love.graphics.setColor(0.8,0.8,0.8)
    love.graphics.print("Paste another encrypted string + password to preview differences.", 40, 55)

    local w = love.graphics.getWidth()

    Basic.textInput("merge.string",   {x=40, y=110, w=w-80, h=44}, {label="Incoming Encrypted String (base64)"})
    Basic.textInput("merge.password", {x=40, y=190, w=w-80, h=44}, {label="Incoming Password", password=true})

    if Basic.button("merge.cancel", {x=40, y=260, w=140, h=44}, "Cancel") then
      App.state = App.prevState or "tasks"
      App.prevState = nil
    end
    if Basic.button("merge.load", {x=200, y=260, w=160, h=44}, "Load Diff") then
      loadMergeDiff()
    end

    love.graphics.setColor(0.7,0.7,0.7)
    love.graphics.print("Enter = Load Diff | Esc = Cancel | F3 = merge test export", 40, 320)

    if App.merge.err then
      love.graphics.setColor(1,0.35,0.35)
      love.graphics.print("Error: "..tostring(App.merge.err), 40, 360)
    end

  elseif App.state == "mergePreview" then
    local diff = App.merge.diff
    if not diff then
      love.graphics.setColor(1,0.35,0.35)
      love.graphics.print("No diff loaded.", 40, 40)
    else
      love.graphics.setColor(1,1,1)
      love.graphics.print("Merge Preview", 40, 30)

      App.merge.optEdits = Basic.checkbox("merge.optEdits", {x=40, y=70, w=280, h=26}, "Import edits (titles/descs)", App.merge.optEdits)
      App.merge.optAssignments = Basic.checkbox("merge.optAssign", {x=340, y=70, w=320, h=26}, "Import assignment changes", App.merge.optAssignments)
      App.merge.optNewTasks = Basic.checkbox("merge.optNew", {x=40, y=100, w=280, h=26}, "Import created tasks", App.merge.optNewTasks)
      App.merge.optDeletedTasks = Basic.checkbox("merge.optDel", {x=340, y=100, w=280, h=26}, "Import deleted tasks", App.merge.optDeletedTasks)

      if Basic.button("merge.back", {x=40, y=136, w=120, h=40}, "Back") then
        App.state = "mergeLogin"
      end
      if Basic.button("merge.apply", {x=180, y=136, w=160, h=40}, "Apply Merge") then
        openConfirm("Apply selected merge options to your current board?", function()
          applyMergeNow()
        end)
      end

      love.graphics.setColor(0.7,0.7,0.7)
      love.graphics.print("Green=new tasks, Red=deleted tasks, Blue=updated tasks. Double-click a task to inspect subtasks.", 370, 146)

      -- unified task list: base tasks + incoming new tasks
      local show, seen = {}, {}
      for _, t in ipairs(App.data.tasks or {}) do
        show[#show+1] = { source="base", id=t.id }
        seen[t.id] = true
      end
      for _, id in ipairs(diff.newTasks or {}) do
        if not seen[id] then show[#show+1] = { source="incoming", id=id } end
      end

      local pseudoTasks = {}
      for _, item in ipairs(show) do
        local t = (item.source=="base" and App.index.taskById[item.id] and App.index.taskById[item.id].task)
                or (item.source=="incoming" and App.merge.incomingIndex.taskById[item.id] and App.merge.incomingIndex.taskById[item.id].task)
        if t then pseudoTasks[#pseudoTasks+1] = t end
      end

      -- GRID (scrollable)
      local yStart = 190
      local rects, contentH = getGridLayoutCached(
        App.layoutCache.mergePreview,
        pseudoTasks,
        { yStart=yStart, margin=30, cardW=260, cardH=120, gap=18 },
        App.mergeVersion -- listKey
      )

      -- clamp for resize/data change/wheel overscroll safety
      App.scroll.mergePreview = clampScroll(App.scroll.mergePreview, contentH, love.graphics.getHeight(), yStart)

      local mx, my = love.mouse.getPosition()
      local hoverTask = nil

      love.graphics.push()
      love.graphics.translate(0, App.scroll.mergePreview)

      for _, task in ipairs(pseudoTasks) do
        local r = rects[task.id]
        local hovered = inside(mx, my - App.scroll.mergePreview, r)
        if hovered then hoverTask = task end

        local bg, border, badge = mergeTaskStyle(task.id)

        love.graphics.setColor(bg[1], bg[2], bg[3])
        love.graphics.rectangle("fill", r.x, r.y, r.w, r.h, 10, 10)

        love.graphics.setColor(border[1], border[2], border[3])
        love.graphics.rectangle("line", r.x, r.y, r.w, r.h, 10, 10)

        love.graphics.setColor(1,1,1)
        love.graphics.printf(task.title, r.x+12, r.y+10, r.w-24)

        if badge ~= "" then
          love.graphics.setColor(border[1], border[2], border[3])
          love.graphics.print(badge, r.x + r.w - 52, r.y + 10)
        end

        love.graphics.setColor(0.75,0.75,0.75)
        love.graphics.print(("Subtasks: %d"):format(#(task.subtasks or {})), r.x+12, r.y+r.h-38)

        local summary = Model.taskAssigneeSummary(task)
        love.graphics.setColor(0.75,0.75,0.75)
        love.graphics.printf("Assigned: "..(summary ~= "" and summary or "(none)"), r.x+12, r.y+r.h-20, r.w-24)
      end

      love.graphics.pop()

      if hoverTask then
        local text = Model.taskHoverText(hoverTask)

        -- if hovered exists in both, append assignment delta details
        if App.index.taskById[hoverTask.id] and App.merge.incomingIndex.taskById[hoverTask.id] then
          local added, removed = assignmentDeltaForTask(hoverTask.id)
          if #added > 0 then text = text .. "\n\nAdded assignees: " .. table.concat(added, ", ") end
          if #removed > 0 then text = text .. "\nRemoved assignees: " .. table.concat(removed, ", ") end
        end

        drawTooltip(text, mx, my)
      end
    end
  elseif App.state == "mergeSubtasks" then
    local taskId = App.merge.viewTaskId
    local bTask = App.index.taskById[taskId] and App.index.taskById[taskId].task
    local nTask = App.merge.incomingIndex.taskById[taskId] and App.merge.incomingIndex.taskById[taskId].task

    love.graphics.setColor(1,1,1)
    love.graphics.print("Merge Subtasks", 40, 30)

    if Basic.button("mergeSub.back", {x=40, y=70, w=120, h=40}, "Back") then
      App.state = "mergePreview"
      App.merge.viewTaskId = nil
    end

    if not bTask or not nTask then
      love.graphics.setColor(1,0.35,0.35)
      love.graphics.print("This task is not present in both versions.", 40, 130)
    else
      love.graphics.setColor(1,1,1)
      love.graphics.print(bTask.title, 180, 78)

      local subs, seen = {}, {}
      for _, s in ipairs(bTask.subtasks or {}) do subs[#subs+1] = {id=s.id, base=s, inc=nil}; seen[s.id]=true end
      for _, s in ipairs(nTask.subtasks or {}) do
        if seen[s.id] then
          for _, row in ipairs(subs) do if row.id==s.id then row.inc=s break end end
        else
          subs[#subs+1] = {id=s.id, base=nil, inc=s}
          seen[s.id]=true
        end
      end

      local fake = {}
      for _, row in ipairs(subs) do
        fake[#fake+1] = { id=row.id, title = (row.inc and row.inc.title) or (row.base and row.base.title) or ("(missing "..row.id..")") }
      end
      local yStart = 140
      local rects, contentH = getGridLayoutCached(
        App.layoutCache.mergeSubtasks,
        fake,
        { yStart=yStart, margin=30, cardW=240, cardH=90, gap=14 },
        tostring(taskId) .. ":" .. tostring(App.mergeVersion)
      )

      -- clamp every frame so wheel + resize are safe
      App.scroll.mergeSubtasks = clampScroll(App.scroll.mergeSubtasks, contentH, love.graphics.getHeight(), yStart)

      local mx, my = love.mouse.getPosition()
      local hoverText = nil

      love.graphics.push()
      love.graphics.translate(0, App.scroll.mergeSubtasks)


      for i, row in ipairs(subs) do
        local r = rects[fake[i].id]
        local hovered = inside(mx, my - App.scroll.mergeSubtasks, r)

        local bA = row.base and row.base.assignee or nil
        local nA = row.inc  and row.inc.assignee  or nil

        local bT = row.base and row.base.title or nil
        local nT = row.inc  and row.inc.title  or nil
        local titleChanged = (bT ~= nT)

        local bg
        if bA == nil and nA ~= nil then
          bg = {0.10, 0.18, 0.12}
        elseif bA ~= nil and nA == nil then
          bg = {0.20, 0.10, 0.10}
        elseif bA ~= nA then
          bg = {0.16, 0.12, 0.18}
        else
          bg = {0.14, 0.14, 0.16}
        end

        if (bA == nA) and titleChanged then
          bg = {0.14, 0.14, 0.20}
        end

        love.graphics.setColor(bg[1], bg[2], bg[3])
        love.graphics.rectangle("fill", r.x, r.y, r.w, r.h, 10, 10)
        love.graphics.setColor(hovered and 0.9 or 0.35, hovered and 0.9 or 0.35, hovered and 0.9 or 0.35)
        love.graphics.rectangle("line", r.x, r.y, r.w, r.h, 10, 10)

        love.graphics.setColor(1,1,1)
        love.graphics.printf(fake[i].title, r.x+12, r.y+10, r.w-24)
        love.graphics.setColor(0.75,0.75,0.75)
        love.graphics.printf(("Before: %s"):format(bA or "(none)"), r.x+12, r.y+r.h-42, r.w-24)
        love.graphics.printf(("After:  %s"):format(nA or "(none)"), r.x+12, r.y+r.h-24, r.w-24)

        if hovered then
          hoverText = ("Before: %s\nAfter: %s"):format(bA or "(none)", nA or "(none)")
        end
      end

      love.graphics.pop()
      if hoverText then drawTooltip(hoverText, mx, my) end
    end
  end

  -- notice
  if App.notice and love.timer.getTime() <= App.notice.untilTime then
    local text = App.notice.text
    local bw = love.graphics.getFont():getWidth(text) + 30
    local x = (love.graphics.getWidth() - bw)/2
    local y = love.graphics.getHeight() - 70
    love.graphics.setColor(0,0,0,0.8)
    love.graphics.rectangle("fill", x, y, bw, 40, 8, 8)
    love.graphics.setColor(1,1,1)
    love.graphics.print(text, x + 15, y + 12)
  end

  -- modal overlay (always on top)
  if App.modal then
    local W,H = love.graphics.getWidth(), love.graphics.getHeight()
    love.graphics.setColor(0,0,0,0.6)
    love.graphics.rectangle("fill", 0,0,W,H)

    local mw, mh = 560, 220
    local mx, my = (W-mw)/2, (H-mh)/2
    love.graphics.setColor(0.12,0.12,0.13)
    love.graphics.rectangle("fill", mx, my, mw, mh, 10, 10)
    love.graphics.setColor(1,1,1,0.35)
    love.graphics.rectangle("line", mx, my, mw, mh, 10, 10)

    if App.modal.kind == "confirm" then
      love.graphics.setColor(1,1,1)
      love.graphics.printf("Confirm", mx+20, my+18, mw-40)
      love.graphics.setColor(0.85,0.85,0.85)
      love.graphics.printf(App.modal.message or "", mx+20, my+60, mw-40)

      if Basic.button("modal.no", {x=mx+mw-220, y=my+mh-54, w=90, h=36}, "No") then
        local cb = App.modal.onNo
        closeModal()
        if cb then cb() end
      end
      if Basic.button("modal.yes", {x=mx+mw-120, y=my+mh-54, w=90, h=36}, "Yes") then
        local cb = App.modal.onYes
        closeModal()
        if cb then cb() end
      end

    elseif App.modal.kind == "prompt" then
      love.graphics.setColor(1,1,1)
      love.graphics.printf(App.modal.title or "Input", mx+20, my+18, mw-40)

      Basic.textInput(App.modal.inputId, {x=mx+20, y=my+80, w=mw-40, h=44}, {
        password = App.modal.opts.password
      })

      if Basic.button("modal.cancel", {x=mx+mw-220, y=my+mh-54, w=90, h=36}, "Cancel") then
        local cb = App.modal.onCancel
        closeModal()
        if cb then cb() end
      end
      if Basic.button("modal.ok", {x=mx+mw-120, y=my+mh-54, w=90, h=36}, "OK") then
        local cb = App.modal.onOk
        local txt = Basic.get(App.modal.inputId)
        if cb then cb(txt) end
      end
    elseif App.modal.kind == "exportPreview" then
      local s = App.modal.exported or ""
      local len = #s

      love.graphics.setColor(1,1,1)
      love.graphics.printf("Export Result", mx+20, my+18, mw-40)

      love.graphics.setColor(0.8,0.8,0.8)
      love.graphics.printf(("Length: %d chars"):format(len), mx+20, my+50, mw-40)

      local function snippet(str, head, tail)
        if #str <= head + tail + 10 then return str end
        return str:sub(1, head) .. "\n...\n" .. str:sub(#str - tail + 1)
      end

      local preview = snippet(s, 120, 120)

      love.graphics.setColor(1,1,1)
      love.graphics.printf(preview, mx+20, my+78, mw-40)

      if Basic.button("exportPrev.copy", {x=mx+mw-220, y=my+mh-54, w=90, h=36}, "Copy") then
        love.system.setClipboardText(s)
        setNotice("Copied", 1.2)
      end
      if Basic.button("exportPrev.close", {x=mx+mw-120, y=my+mh-54, w=90, h=36}, "Close") then
        closeModal()
      end
    end
  end

  Basic.endFrame()
end

function App.mousepressed(x, y, button)
  Basic.mousePressed(x, y, button)

  -- if modal is open, clicks are handled by modal buttons in draw()
  if App.modal then return end

    if App.state == "mergePreview" and button == 1 then
    -- double click a task to view subtask diffs (only for tasks existing in both)
    local pseudoTasks = {}
    do
      local diff = App.merge.diff
      local show, seen = {}, {}
      for _, t in ipairs(App.data.tasks or {}) do show[#show+1]=t.id; seen[t.id]=true end
      for _, id in ipairs(diff.newTasks or {}) do if not seen[id] then show[#show+1]=id end end
      for _, id in ipairs(show) do
        local task = (App.index.taskById[id] and App.index.taskById[id].task)
                 or (App.merge.incomingIndex.taskById[id] and App.merge.incomingIndex.taskById[id].task)
        if task then pseudoTasks[#pseudoTasks+1] = task end
      end
    end

    local yStart = 190
    local rects, contentH = computeGridLayout(pseudoTasks, {
      yStart = yStart, margin=30, cardW=260, cardH=120, gap=18
    })
    App.scroll.mergePreview = clampScroll(App.scroll.mergePreview, contentH, love.graphics.getHeight(), yStart)

    local mx, my = love.mouse.getPosition()
    local hoverTask

    for _, task in ipairs(pseudoTasks) do
      local r = rects[task.id]
      local hovered = inside(mx, my - App.scroll.mergePreview, r)
      if hovered then hoverTask = task end
      if r and inside(x, y - App.scroll.mergePreview, r) then
        local now = love.timer.getTime()
        if App.lastClick.taskId == task.id and (now - App.lastClick.time) <= 0.35 then
          -- only if exists in both
          if App.index.taskById[task.id] and App.merge.incomingIndex.taskById[task.id] then
            App.state = "mergeSubtasks"
            App.merge.viewTaskId = task.id
            App.scroll.mergeSubtasks = 0
          else
            setNotice("Subtask diff only for tasks present in both", 2)
          end
        end
        App.lastClick.time = now
        App.lastClick.taskId = task.id
        return
      end
    end
  end

  -- TASKS VIEW
  if App.state == "tasks" then
    local tasks = App.data.tasks or {}
    local rects, _ = computeGridLayout(tasks, { yStart=110, margin=30, cardW=260, cardH=120, gap=18 })

    -- left click: select + double click open
    if button == 1 then
      for _, task in ipairs(tasks) do
        local r = rects[task.id]
        if r and inside(x, y - App.scroll.tasks, r) then
          App.selectedTaskId = task.id

          local now = love.timer.getTime()
          if App.lastClick.taskId == task.id and (now - App.lastClick.time) <= 0.35 then
            App.state = "subtasks"
            App.viewTaskId = task.id
            App.scroll.subtasks = 0
          end
          App.lastClick.time = now
          App.lastClick.taskId = task.id
          break
        end
      end
      return
    end

    -- right click: mode action on task
    if button == 2 then
      for _, task in ipairs(tasks) do
        local r = rects[task.id]
        if r and inside(x, y - App.scroll.tasks, r) then
          local taskId = task.id
          App.selectedTaskId = taskId

          if App.mode == "delete" then
            openConfirm(("Are you sure you want to delete task: %s?"):format(task.title), function()
              Model.deleteTask(App.data, App.index, taskId)
              setNotice("Task deleted", 1.5)
            end)
            return
          end

          if App.mode == "edit" then
            -- edit title+desc via two prompts (simple)
            openPrompt("Edit task title", task.title, function(newTitle)
              newTitle = (newTitle or ""):gsub("^%s+", ""):gsub("%s+$", "")
              if newTitle == "" then setNotice("Title cannot be empty", 2) return end
              Model.editTask(App.data, App.index, taskId, { title = newTitle })
              closeModal()
              -- now prompt desc
              openPrompt("Edit task description", task.desc or "", function(newDesc)
                Model.editTask(App.data, App.index, taskId, { desc = newDesc or "" })
                closeModal()
              end, function() closeModal() end)
            end, function() closeModal() end)
            return
          end

          -- normal mode: assign task (assign all unassigned subtasks)
          ensureUsernameThen(function(username)
            local ok, err = Model.assignTask(App.data, App.index, App.session, taskId, username)
            if ok then
              setNotice("Assigned available subtasks to "..username, 2)
            else
              if err and err.reason == "no_available_subtasks" then
                setNotice("No subtasks available to assign", 2)
              else
                setNotice("Assign failed", 2)
              end
            end
          end)
          return
        end
      end
    end
  end

  -- SUBTASKS VIEW
  if App.state == "subtasks" then
    local taskEntry = App.index.taskById[App.viewTaskId]
    if not taskEntry then return end
    local task = taskEntry.task
    local subs = task.subtasks or {}
    local rects, _ = computeGridLayout(subs, { yStart=140, margin=30, cardW=240, cardH=90, gap=14 })

    if button == 2 then
      for _, sub in ipairs(subs) do
        local r = rects[sub.id]
        if r and inside(x, y - App.scroll.subtasks, r) then
          local subId = sub.id

          if App.mode == "delete" then
            openConfirm(("Delete subtask: %s?"):format(sub.title), function()
              Model.deleteSubtask(App.data, App.index, subId)
              setNotice("Subtask deleted", 1.5)
            end)
            return
          end

          if App.mode == "edit" then
            openPrompt("Edit subtask title", sub.title, function(newTitle)
              newTitle = (newTitle or ""):gsub("^%s+", ""):gsub("%s+$", "")
              if newTitle == "" then setNotice("Title cannot be empty", 2) return end
              Model.editSubtask(App.data, App.index, subId, { title = newTitle })
              closeModal()
            end, function() closeModal() end)
            return
          end

          -- normal: assign subtask (override requires two confirms)
          ensureUsernameThen(function(username)
            local ok, err = Model.assignSubtask(App.data, App.index, App.session, subId, username, { override = false })
            if ok then
              setNotice("Assigned to "..username, 1.5)
              return
            end

            if err and err.reason == "needs_override" then
              local cur = err.current or "(unknown)"
              openConfirm(("Are you sure you want to override %s's assignment?"):format(cur), function()
                openConfirm(("Are you really sure you want to override %s?"):format(cur), function()
                  local ok2 = Model.assignSubtask(App.data, App.index, App.session, subId, username, { override = true })
                  if ok2 then setNotice("Overrode assignment", 1.5) end
                end)
              end)
            else
              setNotice("Assign failed", 2)
            end
          end)

          return
        end
      end
    end
  end
end

function App.textinput(t)
  Basic.textInputEvent(t)
end

function App.keypressed(key)
  if not App.modal then
    if key == "f2" then
      generateDemoExport()
      return
    end
    if key == "f3" then
      generateMergeTestExport()
      return
    end
  end
  if App.state == "login" then
    if key == "tab" then
      App.ui.active = (App.ui.active == "login.string") and "login.password" or "login.string"
      return
    end
    if key == "return" or key == "kpenter" then
      importNow()
      return
    end
  end

  if ctrlDown() and key == "m" then
      if App.state ~= "login" and App.data then
        startMerge()
      else
        -- if on login, merge doesn't make sense; ignore
        setNotice("Import first before merging", 2)
      end
    return
  end

  if ctrlDown() and key == "s" and App.data and App.state ~= "login" and not App.modal then
    exportNow()
    return
  end

  if App.state == "mergeLogin" then
      if key == "escape" then
        App.state = App.prevState or "tasks"
        App.prevState = nil
        return
      end
      if key == "return" or key == "kpenter" then
        loadMergeDiff()
        return
      end
  end


    -- modal key handling
  if App.modal then
    if App.modal.kind == "exportPreview" then
      if ctrlDown() and key == "c" then
        love.system.setClipboardText(App.modal.exported or "")
        setNotice("Copied", 1.2)
        return
      end
      if key == "escape" then
        closeModal()
        return
      end
    end
    if key == "escape" then
      closeModal()
      return
    end
    if key == "return" or key == "kpenter" then
      if App.modal.kind == "prompt" and App.modal.onOk then
        App.modal.onOk(Basic.get(App.modal.inputId))
      end
      return
    end
    Basic.keyPressed(key)
    return
  end

  -- global hotkeys (tasks/subtasks)
  if (App.state == "tasks" or App.state == "subtasks") and ctrlDown() then
    if key == "e" then
      App.mode = (App.mode == "edit") and "assign" or "edit"
      setNotice("Mode: "..App.mode, 1.2)
      return
    end
    if key == "d" then
      App.mode = (App.mode == "delete") and "assign" or "delete"
      setNotice("Mode: "..App.mode, 1.2)
      return
    end
    if key == "u" then
      openPrompt("Change username", App.session.username or "", function(newName)
        newName = (newName or ""):gsub("^%s+", ""):gsub("%s+$", "")
        if newName == "" then setNotice("Username cannot be empty", 2) return end
        Model.renameSessionUser(App.data, App.index, App.session, newName)
        closeModal()
        setNotice("Username updated", 1.5)
      end, function() closeModal() end)
      return
    end
    if key == "n" then
      -- Ctrl+N: new task modal (simple: title prompt then desc prompt)
      openPrompt("New task title", "", function(title)
        title = (title or ""):gsub("^%s+", ""):gsub("%s+$", "")
        if title == "" then setNotice("Title cannot be empty", 2) return end
        closeModal()
        openPrompt("New task description (optional)", "", function(desc)
          closeModal()
          openPrompt("Initial subtasks (comma-separated, optional)", "", function(subsCsv)
            local subs = {}
            for part in (subsCsv or ""):gmatch("([^,]+)") do
              local s = part:gsub("^%s+", ""):gsub("%s+$", "")
              if s ~= "" then subs[#subs+1] = s end
            end
            if #subs == 0 then subs = nil end
            Model.addTask(App.data, App.index, title, desc or "", subs)
            closeModal()
            setNotice("Task created", 1.5)
          end, function() closeModal() end)
        end, function() closeModal() end)
      end, function() closeModal() end)
      return
    end
  end

  Basic.keyPressed(key)
end

return App