-- src/ui/basic.lua
local Basic = {}

Basic.__index = Basic

function Basic.new()
  return setmetatable({
    active = nil,     -- id of focused input
    widgets = {},     -- optional registry
  }, Basic)
end

local function isInside(x,y, r)
  return x >= r.x and x <= r.x + r.w and y >= r.y and y <= r.y + r.h
end

-- ---------- Button ----------
function Basic.button(id, r, label)
  local ui = Basic._ctx
  local mx, my = love.mouse.getPosition()
  local hover = isInside(mx, my, r)

  love.graphics.setColor(hover and 0.25 or 0.18, hover and 0.25 or 0.18, hover and 0.25 or 0.18)
  love.graphics.rectangle("fill", r.x, r.y, r.w, r.h, 6, 6)
  love.graphics.setColor(1,1,1)
  love.graphics.rectangle("line", r.x, r.y, r.w, r.h, 6, 6)

  local tw = love.graphics.getFont():getWidth(label)
  local th = love.graphics.getFont():getHeight()
  love.graphics.print(label, r.x + (r.w - tw)/2, r.y + (r.h - th)/2)

  if ui._clicked and hover then
    ui._clicked = false
    return true
  end
  return false
end

function Basic.checkbox(id, r, label, value)
  local ui = Basic._ctx
  ui.widgets[id] = ui.widgets[id] or { checked = false }
  local w = ui.widgets[id]
  if value ~= nil then w.checked = not not value end

  local mx, my = love.mouse.getPosition()
  local hover = mx >= r.x and mx <= r.x + r.w and my >= r.y and my <= r.y + r.h

  -- box
  local box = { x = r.x, y = r.y, w = 22, h = 22 }
  love.graphics.setColor(0.12,0.12,0.12)
  love.graphics.rectangle("fill", box.x, box.y, box.w, box.h, 4, 4)
  love.graphics.setColor(hover and 0.9 or 0.45, hover and 0.9 or 0.45, hover and 0.9 or 0.45)
  love.graphics.rectangle("line", box.x, box.y, box.w, box.h, 4, 4)

  if w.checked then
    love.graphics.setColor(0.4, 0.9, 0.5)
    love.graphics.rectangle("fill", box.x+5, box.y+5, box.w-10, box.h-10, 3, 3)
  end

  -- label
  love.graphics.setColor(1,1,1)
  love.graphics.print(label or "", r.x + 30, r.y + 2)

  if ui._clicked and hover then
    ui._clicked = false
    w.checked = not w.checked
  end

  return w.checked
end


-- ---------- TextInput (single-line) ----------
function Basic.textInput(id, r, opts)
  local ui = Basic._ctx
  opts = opts or {}
  ui.widgets[id] = ui.widgets[id] or { text = "", cursor = 0, scroll = 0 }
  local w = ui.widgets[id]

  local mx, my = love.mouse.getPosition()
  local hover = isInside(mx, my, r)
  local focused = (ui.active == id)

  -- background
  love.graphics.setColor(0.12, 0.12, 0.12)
  love.graphics.rectangle("fill", r.x, r.y, r.w, r.h, 6, 6)
  love.graphics.setColor(focused and 0.9 or (hover and 0.6 or 0.35), focused and 0.9 or (hover and 0.6 or 0.35), focused and 0.9 or (hover and 0.6 or 0.35))
  love.graphics.rectangle("line", r.x, r.y, r.w, r.h, 6, 6)

  -- click-to-focus
  if ui._clicked and hover then
    ui._clicked = false
    ui.active = id
    w.cursor = #w.text
  end

  local displayText = w.text
  if opts.password then
    displayText = string.rep("*", #displayText)
  end

  local pad = 10
  local textX = r.x + pad
  local textY = r.y + (r.h - love.graphics.getFont():getHeight())/2

  -- simple horizontal clipping: show the tail if too long
  local maxWidth = r.w - pad*2
  local shown = displayText
  while love.graphics.getFont():getWidth(shown) > maxWidth and #shown > 0 do
    shown = shown:sub(2)
  end

  love.graphics.setColor(1,1,1)
  love.graphics.print(shown, textX, textY)

  -- cursor (only if focused)
  if focused then
    local blink = math.floor(love.timer.getTime() * 2) % 2 == 0
    if blink then
      local cursorX = textX + love.graphics.getFont():getWidth(shown)
      love.graphics.line(cursorX, r.y + 6, cursorX, r.y + r.h - 6)
    end
  end

  -- label (optional)
  if opts.label then
    love.graphics.setColor(1,1,1)
    love.graphics.print(opts.label, r.x, r.y - 20)
  end

  return w.text
end

-- ---------- Input event handling ----------
function Basic.beginFrame()
end

function Basic.endFrame()
  Basic._ctx._clicked = false
end


function Basic.mousePressed(x, y, button)
  if button == 1 then
    Basic._ctx._clicked = true
  end
end

function Basic.textInputEvent(t)
  local ui = Basic._ctx
  if not ui.active then return end
  local w = ui.widgets[ui.active]
  if not w then return end
  -- insert at end (cursor simplified)
  w.text = w.text .. t
  w.cursor = #w.text
end

function Basic.keyPressed(key)
  local ui = Basic._ctx
  if not ui.active then return end
  local w = ui.widgets[ui.active]
  if not w then return end

  local ctrl = love.keyboard.isDown("lctrl") or love.keyboard.isDown("rctrl")

  if ctrl and key == "v" then
    local clip = love.system.getClipboardText() or ""
    w.text = w.text .. clip
    w.cursor = #w.text
    return
  end

  if key == "backspace" then
    w.text = w.text:sub(1, math.max(0, #w.text - 1))
    w.cursor = #w.text
  end
end

-- Helper to get/set a field programmatically
function Basic.get(id)
  local w = Basic._ctx.widgets[id]
  return w and w.text or ""
end

function Basic.set(id, text)
  Basic._ctx.widgets[id] = Basic._ctx.widgets[id] or { text = "", cursor = 0, scroll = 0 }
  Basic._ctx.widgets[id].text = text or ""
  Basic._ctx.widgets[id].cursor = #(text or "")
end

-- Call once after creation: Basic.bind(ui)
function Basic.bind(ui)
  Basic._ctx = ui
end

return Basic