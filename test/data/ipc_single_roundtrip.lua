local Module = require "shuttlesock.module"
local Watcher = require "shuttlesock.watcher"
local Process = require "shuttlesock.process"
local Shuso = require "shuttlesock"
local Atomics = require "shuttlesock.atomics"
local IPC = require "shuttlesock.ipc"
local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0",
}

function testmod:initialize()
  self.atomic = Atomics.new("received_reply")
  self.atomic.received_reply = 0
end

testmod:subscribe("core:manager.start", function(self)
  coroutine.wrap(function()
    while true do
      local data, sender = IPC.receive("hello")
      assert(data[1]=="yeep")
      assert(data[2][1]==100)
      assert(data.ok=="okay")
      IPC.send(sender, "hello to you too", {"this message was for worker", sender})
    end
  end)()
end)

testmod:subscribe("core:worker.workers_started", function(self)
  coroutine.wrap(function()
    IPC.send("manager", "yekh", {})
    IPC.send("manager", "hello", {"yeep", {100}, ok="okay"})
    
    local data, sender = IPC.receive("hello to you too")
    assert(sender == -1)
    assert(data[1]=="this message was for worker")
    assert(data[2]==Process.procnum())
    self.atomic.received_reply:increment(1)
    if self.atomic.received_reply:value() == Process.count_workers() then
      Shuso.stop()
    end
  end)()
end)

assert(testmod:add())
