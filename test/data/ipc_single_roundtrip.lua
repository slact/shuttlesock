local Module = require "shuttlesock.module"
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
    local receiver = IPC.Receiver.start("hello")
    while true do
      local data, sender = receiver:yield()
      if not data and sender == "canceled" then
        return
      end
      assert(data[1]=="yeep")
      assert(data[2][1]==100)
      assert(data.ok=="okay")
      IPC.send(sender, "hello to you too", {"this message was for worker", sender})
    end
  end)()
end)

testmod:subscribe("core:worker.workers_started", function(self)
  coroutine.wrap(function()
    local receiver = IPC.Receiver.start("hello to you too")
    IPC.send("manager", "yekh", {})
    IPC.send("manager", "hello", {"yeep", {100}, ok="okay"})
    
    local data, sender = receiver:yield()
    assert(sender == -1)
    assert(data[1]=="this message was for worker")
    assert(data[2]==Process.procnum())
    self.atomic:increment("received_reply", 1)
    if self.atomic.received_reply == Process.count_workers() then
      Shuso.stop()
    end
  end)()
end)

assert(testmod:add())
