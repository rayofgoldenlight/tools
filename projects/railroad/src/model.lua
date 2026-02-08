-- src/model.lua
local Model = {}

-- ---------- 1) ID helpers ----------

local function parseId(prefix, id)
  -- returns numeric portion or nil
  if type(id) ~= "string" then return nil end
  local n = id:match("^" .. prefix .. "(%d+)$")
  return n and tonumber(n) or nil
end

local function makeTaskId(n)    return "t" .. tostring(n) end
local function makeSubtaskId(n) return "s" .. tostring(n) end

-- ---------- 2) Normalize + counter repair ----------

function Model.normalize(data)
  assert(type(data) == "table", "data must be a table")
  data.version = tonumber(data.version) or 1

  if type(data.meta) ~= "table" then data.meta = {} end
  data.meta.nextTaskId = tonumber(data.meta.nextTaskId) or 1
  data.meta.nextSubtaskId = tonumber(data.meta.nextSubtaskId) or 1

  if type(data.tasks) ~= "table" then data.tasks = {} end

  for _, task in ipairs(data.tasks) do
    task.title = tostring(task.title or "")
    task.desc  = tostring(task.desc or "")
    task.assignee = nil
    if type(task.subtasks) ~= "table" then task.subtasks = {} end

    for _, sub in ipairs(task.subtasks) do
      sub.title = tostring(sub.title or "")
      if sub.assignee == "" then sub.assignee = nil end
    end
  end

  return data
end

-- Recommended: after import/merge, ensure counters are >= (max existing id + 1)
function Model.repairCounters(data)
  data = Model.normalize(data)

  local maxT, maxS = 0, 0
  for _, task in ipairs(data.tasks) do
    local tn = parseId("t", task.id)
    if tn and tn > maxT then maxT = tn end

    for _, sub in ipairs(task.subtasks) do
      local sn = parseId("s", sub.id)
      if sn and sn > maxS then maxS = sn end
    end
  end

  if data.meta.nextTaskId <= maxT then data.meta.nextTaskId = maxT + 1 end
  if data.meta.nextSubtaskId <= maxS then data.meta.nextSubtaskId = maxS + 1 end
  return data
end

-- ---------- 3) ID allocation ----------

function Model.allocTaskId(data)
  data = Model.normalize(data)
  local n = data.meta.nextTaskId
  data.meta.nextTaskId = n + 1
  return makeTaskId(n)
end

function Model.allocSubtaskId(data)
  data = Model.normalize(data)
  local n = data.meta.nextSubtaskId
  data.meta.nextSubtaskId = n + 1
  return makeSubtaskId(n)
end

-- ---------- 4) Index rebuild ----------

-- index.taskById[taskId] = { task = <ref>, taskIndex = i }
-- index.subById[subId]   = { sub = <ref>, task = <ref>, taskId = "...", subIndex = j }
function Model.rebuildIndex(data)
  data = Model.normalize(data)

  local index = {
    taskById = {},
    subById  = {},
  }

  for i, task in ipairs(data.tasks) do
    if type(task.id) ~= "string" or task.id == "" then
      error(("Task at position %d is missing an id"):format(i))
    end
    if index.taskById[task.id] then
      error(("Duplicate task id: %s"):format(task.id))
    end

    index.taskById[task.id] = { task = task, taskIndex = i }

    for j, sub in ipairs(task.subtasks) do
      if type(sub.id) ~= "string" or sub.id == "" then
        error(("Subtask at task %s position %d missing id"):format(task.id, j))
      end
      if index.subById[sub.id] then
        error(("Duplicate subtask id (global): %s"):format(sub.id))
      end
      index.subById[sub.id] = { sub = sub, task = task, taskId = task.id, subIndex = j }
    end
  end

  return index
end

-- Fix taskIndex entries after a task is removed (array indices shift)
local function fixTaskIndicesAfter(data, index, startAt)
  for i = startAt, #data.tasks do
    local t = data.tasks[i]
    local e = index.taskById[t.id]
    if e then e.taskIndex = i end
  end
end

-- Fix subIndex entries after a subtask is removed from one task
local function fixSubIndicesAfter(task, index, startAt)
  for j = startAt, #task.subtasks do
    local s = task.subtasks[j]
    local e = index.subById[s.id]
    if e then e.subIndex = j end
  end
end

---Adds a new task. Returns taskId.
---subtaskTitles: optional array of strings. Subtasks have no descriptions.
function Model.addTask(data, index, title, desc, subtaskTitles)
  data = Model.normalize(data)
  assert(index and index.taskById and index.subById, "index required (use Model.rebuildIndex once at load)")

  local taskId = Model.allocTaskId(data)
  local task = {
    id = taskId,
    title = tostring(title or ""),
    desc = tostring(desc or ""),
    assignee = nil,
    subtasks = {}
  }

  -- append task
  table.insert(data.tasks, task)
  index.taskById[taskId] = { task = task, taskIndex = #data.tasks }

  -- optional initial subtasks
  if type(subtaskTitles) == "table" then
    for _, stitle in ipairs(subtaskTitles) do
      local subId = Model.allocSubtaskId(data)
      local sub = { id = subId, title = tostring(stitle or ""), assignee = nil }
      table.insert(task.subtasks, sub)
      index.subById[subId] = { sub = sub, task = task, taskId = taskId, subIndex = #task.subtasks }
    end
  end

  return taskId
end

---Adds a subtask to an existing task. Returns subtaskId.
function Model.addSubtask(data, index, taskId, title)
  assert(index and index.taskById, "index required")
  local taskEntry = index.taskById[taskId]
  assert(taskEntry, "task not found: " .. tostring(taskId))

  local task = taskEntry.task
  local subId = Model.allocSubtaskId(data)
  local sub = { id = subId, title = tostring(title or ""), assignee = nil }

  table.insert(task.subtasks, sub)
  index.subById[subId] = { sub = sub, task = task, taskId = taskId, subIndex = #task.subtasks }
  return subId
end

function Model.editTask(data, index, taskId, fields)
  assert(type(fields) == "table", "fields must be a table")
  local taskEntry = index.taskById[taskId]
  assert(taskEntry, "task not found: " .. tostring(taskId))

  local task = taskEntry.task
  if fields.title ~= nil then task.title = tostring(fields.title) end
  if fields.desc  ~= nil then task.desc  = tostring(fields.desc) end
  return true
end

function Model.editSubtask(data, index, subId, fields)
  assert(type(fields) == "table", "fields must be a table")
  local subEntry = index.subById[subId]
  assert(subEntry, "subtask not found: " .. tostring(subId))

  local sub = subEntry.sub
  if fields.title ~= nil then sub.title = tostring(fields.title) end
  return true
end

function Model.deleteTask(data, index, taskId)
  assert(index and index.taskById and index.subById, "index required")
  local taskEntry = index.taskById[taskId]
  if not taskEntry then return false end

  local taskIndex = taskEntry.taskIndex
  local task = taskEntry.task

  -- remove subtasks from sub index first
  for _, sub in ipairs(task.subtasks) do
    index.subById[sub.id] = nil
  end

  -- remove task from data + index
  table.remove(data.tasks, taskIndex)
  index.taskById[taskId] = nil

  -- fix shifted task indices
  fixTaskIndicesAfter(data, index, taskIndex)
  return true
end

function Model.deleteSubtask(data, index, subId)
  assert(index and index.subById, "index required")
  local subEntry = index.subById[subId]
  if not subEntry then return false end

  local task = subEntry.task
  local subIndex = subEntry.subIndex

  table.remove(task.subtasks, subIndex)
  index.subById[subId] = nil

  -- fix shifted sub indices for remaining subtasks in that task
  fixSubIndicesAfter(task, index, subIndex)
  return true
end

function Model.newSession()
  return { username = nil, assignedRefs = {} }
end

local function refKey(kind, id)
  return kind .. ":" .. id
end

local function recordAssigned(session, kind, id)
  if not session then return end
  session.assignedRefs = session.assignedRefs or {}
  session.assignedRefs[refKey(kind, id)] = true
end

-- Returns number of unassigned subtasks (availability for "assign task")
function Model.countUnassignedSubtasks(index, taskId)
  local e = index.taskById[taskId]
  assert(e, "task not found: " .. tostring(taskId))
  local task = e.task

  local count = 0
  for _, sub in ipairs(task.subtasks or {}) do
    if sub.assignee == nil then count = count + 1 end
  end
  return count
end

function Model.taskCanAssign(index, taskId)
  return Model.countUnassignedSubtasks(index, taskId) > 0
end

function Model.subtaskNeedsOverride(index, subId, username)
  local e = index.subById[subId]
  assert(e, "subtask not found: " .. tostring(subId))
  local cur = e.sub.assignee
  if cur ~= nil and cur ~= username then
    return true, cur
  end
  return false, cur
end

-- Assigning a task = assign ALL currently-unassigned subtasks.
-- If none are available, returns false with reason "no_available_subtasks".
function Model.assignTask(data, index, session, taskId, username)
  assert(type(username) == "string" and username ~= "", "username required")

  local entry = index.taskById[taskId]
  assert(entry, "task not found: " .. tostring(taskId))
  local task = entry.task

  local assignedAny = false
  for _, sub in ipairs(task.subtasks) do
    if sub.assignee == nil then
      sub.assignee = username
      recordAssigned(session, "sub", sub.id)
      assignedAny = true
    end
  end

  if not assignedAny then
    return false, { reason = "no_available_subtasks" }
  end

  return true
end

-- opts = { override = false }
function Model.assignSubtask(data, index, session, subId, username, opts)
  assert(type(username) == "string" and username ~= "", "username required")
  opts = opts or {}

  local entry = index.subById[subId]
  assert(entry, "subtask not found: " .. tostring(subId))

  local sub = entry.sub
  local cur = sub.assignee
  if cur ~= nil and cur ~= username and not opts.override then
    return false, { reason = "needs_override", current = cur }
  end

  sub.assignee = username
  recordAssigned(session, "sub", subId)
  return true
end

function Model.renameSessionUser(data, index, session, newName)
  assert(session, "session required")
  assert(type(newName) == "string" and newName ~= "", "newName required")

  local old = session.username
  session.username = newName

  if not old or old == "" then
    return true -- nothing to rewrite yet
  end

  for k, _ in pairs(session.assignedRefs or {}) do
    local kind, id = k:match("^(%w+):(.+)$")
    if kind == "sub" then
        local se = index.subById[id]
        if se and se.sub.assignee == old then
            se.sub.assignee = newName
        end
    end
  end

  return true
end

local function uniqueAssigneesFromSubtasks(task)
  local seen, out = {}, {}
  for _, sub in ipairs(task.subtasks or {}) do
    local a = sub.assignee
    if type(a) == "string" and a ~= "" and not seen[a] then
      seen[a] = true
      out[#out+1] = a
    end
  end
  table.sort(out)
  return out
end

function Model.taskAssigneeSummary(task)
  local names = uniqueAssigneesFromSubtasks(task)
  if #names == 0 then return "" end
  return table.concat(names, ", ")
end

-- What you can show in tooltip: desc + "Assigned: ..."
function Model.taskHoverText(task)
  local desc = tostring(task.desc or "")
  local summary = Model.taskAssigneeSummary(task)
  if summary ~= "" then
    if desc ~= "" then
      return desc .. "\nAssigned: " .. summary
    else
      return "Assigned: " .. summary
    end
  end
  return desc
end

-- diff = {
--   newTasks = { taskId, ... }                  -- ids in incoming not in base
--   deletedTasks = { taskId, ... }              -- ids in base not in incoming
--   updatedTasks = {
--     [taskId] = {
--       titleChanged = {from=..., to=...} or nil,
--       descChanged  = {from=..., to=...} or nil,
--       subtasks = {
--         added   = { subId, ... },
--         deleted = { subId, ... },
--         titleChanged = { [subId] = {from=..., to=...}, ... },
--         assigneeChanged = { [subId] = {from=..., to=...}, ... },
--       },
--       assigneeSummaryChanged = {from="...", to="..."} or nil, -- derived from subtasks
--     }
--   }
-- }
function Model.computeMergeDiff(baseData, baseIndex, incomingData, incomingIndex)
  local diff = {
    newTasks = {},
    deletedTasks = {},
    updatedTasks = {}
  }

  -- deleted = in base, not in incoming
  for taskId, _ in pairs(baseIndex.taskById) do
    if not incomingIndex.taskById[taskId] then
      diff.deletedTasks[#diff.deletedTasks+1] = taskId
    end
  end

  -- new = in incoming, not in base
  for taskId, _ in pairs(incomingIndex.taskById) do
    if not baseIndex.taskById[taskId] then
      diff.newTasks[#diff.newTasks+1] = taskId
    end
  end

  table.sort(diff.newTasks)
  table.sort(diff.deletedTasks)

  -- updated/assignment-changed = exists in both
  for taskId, baseEntry in pairs(baseIndex.taskById) do
    local incEntry = incomingIndex.taskById[taskId]
    if incEntry then
      local b = baseEntry.task
      local n = incEntry.task

      local u = {
        subtasks = { added = {}, deleted = {}, titleChanged = {}, assigneeChanged = {} }
      }
      local changed = false

      if b.title ~= n.title then
        u.titleChanged = { from = b.title, to = n.title }; changed = true
      end
      if b.desc ~= n.desc then
        u.descChanged = { from = b.desc, to = n.desc }; changed = true
      end

      -- subtask maps
      local bSub = {}
      for _, s in ipairs(b.subtasks or {}) do bSub[s.id] = s end
      local nSub = {}
      for _, s in ipairs(n.subtasks or {}) do nSub[s.id] = s end

      -- detect deleted/changed in base subtasks
      for subId, bs in pairs(bSub) do
        local ns = nSub[subId]
        if not ns then
          u.subtasks.deleted[#u.subtasks.deleted+1] = subId
          changed = true
        else
          if bs.title ~= ns.title then
            u.subtasks.titleChanged[subId] = { from = bs.title, to = ns.title }
            changed = true
          end
          local ba = bs.assignee
          local na = ns.assignee
          if ba ~= na then
            u.subtasks.assigneeChanged[subId] = { from = ba, to = na }
            changed = true
          end
        end
      end

      -- detect added subtasks in incoming
      for subId, _ in pairs(nSub) do
        if not bSub[subId] then
          u.subtasks.added[#u.subtasks.added+1] = subId
          changed = true
        end
      end

      table.sort(u.subtasks.added)
      table.sort(u.subtasks.deleted)

      -- derived summary change (what you show after description)
      local bSum = Model.taskAssigneeSummary(b)
      local nSum = Model.taskAssigneeSummary(n)
      if bSum ~= nSum then
        u.assigneeSummaryChanged = { from = bSum, to = nSum }
        changed = true
      end

      if changed then
        diff.updatedTasks[taskId] = u
      end
    end
  end

  return diff
end

local function importCloneTask(baseData, baseIndex, incomingTask)
  -- allocate new ids in base, copy over fields + assignees
  local newTaskId = Model.allocTaskId(baseData)
  local t = {
    id = newTaskId,
    title = tostring(incomingTask.title or ""),
    desc  = tostring(incomingTask.desc or ""),
    assignee = nil, -- task-level is not authoritative in your rules; keep nil
    subtasks = {}
  }

  table.insert(baseData.tasks, t)
  baseIndex.taskById[newTaskId] = { task = t, taskIndex = #baseData.tasks }

  for _, incSub in ipairs(incomingTask.subtasks or {}) do
    local newSubId = Model.allocSubtaskId(baseData)
    local s = {
      id = newSubId,
      title = tostring(incSub.title or ""),
      assignee = incSub.assignee
    }
    table.insert(t.subtasks, s)
    baseIndex.subById[newSubId] = { sub = s, task = t, taskId = newTaskId, subIndex = #t.subtasks }
  end

  return newTaskId
end

local function insertIncomingSubtaskIntoExistingTask(baseData, baseIndex, baseTaskId, incomingSub)
  local taskEntry = baseIndex.taskById[baseTaskId]
  assert(taskEntry, "base task missing: " .. tostring(baseTaskId))
  local task = taskEntry.task

  -- If the incoming subId is unused globally, we can keep it.
  local desiredId = incomingSub.id
  local finalId = desiredId

  if desiredId and not baseIndex.subById[desiredId] then
    finalId = desiredId
    -- bump counter if needed so future allocations stay unique
    local n = parseId("s", desiredId)
    if n and baseData.meta.nextSubtaskId <= n then
      baseData.meta.nextSubtaskId = n + 1
    end
  else
    -- collision or missing id: allocate a new one
    finalId = Model.allocSubtaskId(baseData)
  end

  local s = {
    id = finalId,
    title = tostring(incomingSub.title or ""),
    assignee = incomingSub.assignee
  }

  table.insert(task.subtasks, s)
  baseIndex.subById[finalId] = { sub = s, task = task, taskId = baseTaskId, subIndex = #task.subtasks }
  return finalId
end

-- opts = {
--   importNewTasks=true/false,
--   importDeletions=true/false,
--   importAssignments=true/false,
--   importEdits=true/false,        -- titles/descs + subtask titles + subtask add/delete within existing tasks
-- }
function Model.applyMerge(baseData, baseIndex, incomingData, incomingIndex, diff, opts)
  opts = opts or {}

  -- 1) deletions
  if opts.importDeletions then
    for _, taskId in ipairs(diff.deletedTasks or {}) do
      if baseIndex.taskById[taskId] then
        Model.deleteTask(baseData, baseIndex, taskId)
      end
    end
  end

  -- 2) new tasks (re-ID as requested)
  if opts.importNewTasks then
    for _, incomingTaskId in ipairs(diff.newTasks or {}) do
      local incEntry = incomingIndex.taskById[incomingTaskId]
      if incEntry then
        importCloneTask(baseData, baseIndex, incEntry.task)
      end
    end
  end

  -- 3) edits + assignments for tasks existing in both
  for taskId, upd in pairs(diff.updatedTasks or {}) do
    local bTaskEntry = baseIndex.taskById[taskId]
    local nTaskEntry = incomingIndex.taskById[taskId]
    if bTaskEntry and nTaskEntry then
      local bTask = bTaskEntry.task
      local nTask = nTaskEntry.task

      if opts.importEdits then
        -- task edits
        bTask.title = nTask.title
        bTask.desc  = nTask.desc

        -- subtask deletions (by id)
        for _, subId in ipairs((upd.subtasks and upd.subtasks.deleted) or {}) do
          if baseIndex.subById[subId] then
            Model.deleteSubtask(baseData, baseIndex, subId)
          end
        end

        -- subtask additions (pull from incoming by id)
        local nSubById = {}
        for _, ns in ipairs(nTask.subtasks or {}) do nSubById[ns.id] = ns end
        for _, subId in ipairs((upd.subtasks and upd.subtasks.added) or {}) do
          local incomingSub = nSubById[subId]
          if incomingSub then
            insertIncomingSubtaskIntoExistingTask(baseData, baseIndex, taskId, incomingSub)
          end
        end

        -- subtask title changes on shared ids
        for subId, _ in pairs((upd.subtasks and upd.subtasks.titleChanged) or {}) do
          local be = baseIndex.subById[subId]
          local ne = incomingIndex.subById[subId]
          if be and ne then
            be.sub.title = ne.sub.title
          end
        end
      end

      if opts.importAssignments then
        -- assignments on shared subtasks only (existing-in-both rule)
        local bSub = {}
        for _, s in ipairs(bTask.subtasks or {}) do bSub[s.id] = s end
        for _, ns in ipairs(nTask.subtasks or {}) do
          local bs = bSub[ns.id]
          if bs then
            bs.assignee = ns.assignee
          end
        end
      end
    end
  end

  return true
end

-- ---------- 5) Convenience getters ----------

function Model.getTask(data, index, taskId)
  local entry = index and index.taskById and index.taskById[taskId]
  if entry then return entry.task end

  -- fallback slow path (if you don't have an index yet)
  for _, t in ipairs(data.tasks) do
    if t.id == taskId then return t end
  end
  return nil
end

function Model.getSubtask(data, index, subId)
  local entry = index and index.subById and index.subById[subId]
  if entry then return entry.sub, entry.task end

  for _, t in ipairs(data.tasks) do
    for _, s in ipairs(t.subtasks) do
      if s.id == subId then return s, t end
    end
  end
  return nil, nil
end

-- convenience constructor
function Model.newEmpty()
  return Model.normalize({
    version = 1,
    meta = { nextTaskId = 1, nextSubtaskId = 1 },
    tasks = {}
  })
end

return Model