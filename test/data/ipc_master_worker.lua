local Module = require "shuttlesock.module"
local IPC = require "shuttlesock.ipc"
local Shuso = require "shuttlesock"
local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0",
}

testmod:subscribe("core:worker.start", function(self)
  coroutine.wrap(function()
    local data, sender = IPC.receive("to-worker-from-master")
    assert(data.str =="foobar")
    assert(data.bool == false)
    assert(data.num == 11)
    assert(data.tbl.first == "firsty")
    Shuso.stop()
  end)()
end)


testmod:subscribe("core:master.workers_started", function(self)
  coroutine.wrap(function()
    
    assert(IPC.send(0, "to-worker-from-master", {
      str = "foobar",
      bool = false,
      num = 11,
      tbl = {
        first = "firsty"
      }
    }))
  end)()
end)

testmod:add()
