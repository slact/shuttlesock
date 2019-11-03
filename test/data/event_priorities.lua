local Module = require "shuttlesock.module"
local Watcher = require "shuttlesock.watcher"
local Process = require "shuttlesock.process"
local Shuso = require "shuttlesock"

local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0",
}

testmod:subscribe("core:manager.stop", function(self)
  assert(self.n == "ended")
end)

testmod:subscribe("core:manager.workers_started", function(self)
  self.n = "ended"
  Shuso.stop()
end, Module.LAST_PRIORITY)

testmod:subscribe("core:manager.workers_started", function(self)
  assert(self.n==1)
  self.n = self.n+1
end, Module.FIRST_PRIORITY-10)

testmod:subscribe("core:manager.workers_started", function(self)
  assert(self.n==0)
  self.n = self.n+1
end, Module.FIRST_PRIORITY)

testmod:subscribe("core:manager.workers_started", function(self)
  assert(self.n==2)
  self.n = self.n+1
end)

testmod:subscribe("core:manager.workers_started", function(self)
  assert(self.n==4)
  self.n = self.n+1
end, -11)

testmod:subscribe("core:manager.workers_started", function(self)
  assert(self.n==3)
  self.n = self.n+1
end, -10)


function testmod:initialize()
  self.n=0
end

assert(testmod:add())
