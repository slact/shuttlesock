local Module = require "shuttlesock.module"
local Watcher = require "shuttlesock.watcher"

local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0",
  subscribe={"core:master.start", "core:manager.start"},
}

function testmod:initialize()
  local w = Watcher.new("timer")
  w.after=1.0
  w.handler=function(...)
    print("yeah?!")
    require"mm"({...})
  end
  assert(w:start())
end

assert(testmod:add())
