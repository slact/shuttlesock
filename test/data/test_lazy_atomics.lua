local Module = require "shuttlesock.module"
local Watcher = require "shuttlesock.watcher"
local Process = require "shuttlesock.process"
local Shuso = require "shuttlesock"
local Atomics = require "shuttlesock.atomics"

local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0",
}

testmod:subscribe("core:manager.workers_started", function(self)
  self.shared.workers_count = Process.count_workers()
  coroutine.wrap(function()
    Watcher.timer(0.3):yield()
    if Process.type() == "manager" then
      Shuso.stop()
    end
  end)()
end)

testmod:subscribe("core:worker.start", function(self)
  self.shared.workers_started:increment(1)
  self.shared.bar = tostring(self.shared.bar:value()).. "1"
  self.shared.foo = nil
  self.shared.foo = 1
  self.shared.foo = "yes"
  self.shared.foo = false
end)

testmod:subscribe("core:worker.stop", function(self)
  self.shared.workers_stopped:increment(1)
end)

testmod:subscribe("core:master.stop", function(self)
  assert(self.shared.workers_started:value()==self.shared.workers_count:value())
  assert(self.shared.workers_stopped:value()==self.shared.workers_count:value())
  assert(self.shared.foo:value()=="yes")
  assert(self.shared.bar:value()=="01111")
end)


function testmod:initialize()
  self.shared = Atomics.new{"foo", "bar", "workers_started", "workers_stopped", "workers_count"}
  self.shared.foo = 0
  assert(self.shared.foo:value() == 0)
  self.shared.bar="0"
  assert(self.shared.bar:value() == "0")
  self.shared.workers_started = 0
  assert(self.shared.workers_started:value() == 0)
  self.shared.workers_stopped = 0
  assert(self.shared.workers_stopped:value() == 0)
end

assert(testmod:add())
