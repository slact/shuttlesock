local Module = require "shuttlesock.module"
local IPC = require "shuttlesock.ipc"
local Shuso = require "shuttlesock"
local Log = require "shuttlesock.log"
local Spinlock = require "shuttlesock.debug.spinlock"

local MESSAGES_NUM = 1000

local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0",
}

function testmod:initialize()
  self.spin = assert(Spinlock.new())
end

testmod:subscribe("core:master.start", function(self)
  Log.debug("spinlock...")
  self.spin:lock()
  Log.debug("spinlock acquired")
end)

testmod:subscribe("core:master.workers_started", function(self)
  for i=1,MESSAGES_NUM do
    IPC.send("manager", "hello", i)
  end
  Log.debug("sent messages")
  self.spin:unlock()
  Log.debug("spin unlocked")
end)

testmod:subscribe("core:manager.workers_started", function(self)
  coroutine.wrap(function()
  Log.debug("spinlock...")
    self.spin:lock()
    Log.debug("spinlock acquired")
    local received = 0
    local receiver = IPC.Receiver.start("hello")
    while receiver:yield() do
      received = received + 1
      if received == MESSAGES_NUM then break end
    end
    assert(received == MESSAGES_NUM)
    self.spin:unlock()
    self.spin:destroy()
    Shuso.stop()
  end)()
end)

testmod:add()
