local Module = require "shuttlesock.module"
local Watcher = require "shuttlesock.watcher"
local Process = require "shuttlesock.process"
local Shuso = require "shuttlesock"

local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0",
  subscribe={"core:manager.workers_started"},
  publish = {
    "foo",
    "bar",
    "baz"
  }
}


testmod:subscribe("core:manager.workers_started", function(self)
  self:publish("foo")
end)



local subby = Module.new("lua_optsub", "0.0.0")

subby:subscribe("~lua_testmod:foo", function(self)
  self.optfoo = true
  if self.foo and self.optfoo then
    Shuso.stop()
  end
end)
subby:subscribe("lua_testmod:foo", function(self)
  self.foo = true
  if self.foo and self.optfoo then
    Shuso.stop()
  end
end)
subby:subscribe("~lua_testmod:nonexistent", function(self)
  error("this event should not happen")
end)


assert(testmod:add())
assert(subby:add())
