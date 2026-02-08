local App = require("src.app")

function love.load() App.load() end
function love.update(dt) App.update(dt) end
function love.draw() App.draw() end

function love.mousepressed(x,y,b) App.mousepressed(x,y,b) end
function love.textinput(t) App.textinput(t) end
function love.keypressed(k) App.keypressed(k) end

function love.wheelmoved(dx, dy) App.wheelmoved(dx, dy) end