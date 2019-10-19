local Module = require "shuttlesock.module"
local Watcher = require "shuttlesock.watcher"
local Process = require "shuttlesock.process"
local Shuso = require "shuttlesock"

local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0",
  subscribe={"core:manager.workers_started"},
}

function testmod:initialize()
  self:subscribe("core:manager.workers_started", function()
    local w = Watcher.timer(1, function(...)
      --do nothing
    end)
    assert(w:start())
    
    coroutine.wrap(function()
      Watcher.timer(0.1):yield()
      if Process.type() == "manager" then
        Shuso.stop()
      end
    end)()
  end)
end

assert(testmod:add())
