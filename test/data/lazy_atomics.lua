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
  self.shared.bar = tostring(self.shared.bar:value()).. "1" --not atomic
end)

testmod:subscribe("core:worker.stop", function(self)
  self.shared.workers_stopped:increment(1)
end)

testmod:subscribe("core:master.stop", function(self)
  assert(self.shared.workers_started:value()==self.shared.workers_count:value())
  assert(self.shared.workers_stopped:value()==self.shared.workers_count:value())
  assert(self.shared.foo:value()==Shuso.pointer())
  assert(type(self.shared.bar:value())=="string")
  local foo = self.shared.foo
  self.shared:destroy()
  assert(not foo:value(11))
  assert(not foo:set(11))
end)


function testmod:initialize()
  self.shared = Atomics.new("foo", "bar", "workers_started", "workers_stopped", "workers_count")
  self.shared.foo = 0
  assert(self.shared.foo:value() == 0)
  self.shared.bar="0"
  assert(self.shared.bar:value() == "0")
  self.shared.workers_started = 0
  assert(self.shared.workers_started:value() == 0)
  self.shared.workers_stopped = 0
  assert(self.shared.workers_stopped:value() == 0)
  
  self.shared.foo = nil
  assert(self.shared.foo:value() == nil)
  assert(not self.shared.foo:increment(2))
  self.shared.foo = 1.121
  assert(self.shared.foo:value() == 1.121)
  assert(self.shared:set("foo", 1.2))
  assert(self.shared:get("foo"):value() == 1.2)
  assert(not self.shared:increment("foo", 2))
  self.shared.foo = "yes"
  assert(self.shared.foo:value() == "yes")
  assert(not self.shared.foo:increment(2))
  self.shared.foo = false
  assert(self.shared.foo:value() == false, tostring(self.shared.foo:value()))
  assert(not self.shared.foo:increment(2))
  self.shared.foo = Shuso.pointer()
  assert(type(self.shared.foo:value()) == "userdata")
  assert(self.shared.foo:value() == Shuso.pointer())
  assert(not self.shared.foo:increment(2))
  
  assert(not self.shared.foo:set({}))
  
end

assert(testmod:add())
