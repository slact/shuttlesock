local Module = require "shuttlesock.module"
local IPC = require "shuttlesock.ipc"
local Shuso = require "shuttlesock"
local Log = require "shuttlesock.log"
local Mutex = require "shuttlesock.debug.mutex"

local MESSAGES_NUM = 1000

local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0",
}

function testmod:initialize()
  self.mutex = assert(Mutex.new())
end

testmod:subscribe("core:master.start", function(self)
  Log.debug("mutex lock...")
  assert(self.mutex:trylock())
  Log.debug("mutex lock acquired")
end)

testmod:subscribe("core:master.workers_started", function(self)
  for i=1,MESSAGES_NUM do
    IPC.send("manager", "hello", i)
  end
  Log.debug("sent messages")
  self.mutex:unlock()
  Log.debug("mutex unlocked")
end)

testmod:subscribe("core:manager.workers_started", function(self)
  coroutine.wrap(function()
  Log.debug("mutex lock...")
    self.mutex:lock()
    Log.debug("mutex lock acquired")
    local received = 0
    local receiver = IPC.Receiver.start("hello")
    while receiver:yield() do
      received = received + 1
      if received == MESSAGES_NUM then break end
    end
    assert(received == MESSAGES_NUM)
    self.mutex:unlock()
    self.mutex:destroy()
    Shuso.stop()
  end)()
end)

testmod:add()
