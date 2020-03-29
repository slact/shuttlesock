local Module = require "shuttlesock.module"
local Watcher = require "shuttlesock.watcher"
local Process = require "shuttlesock.process"
local Shuso = require "shuttlesock"

local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0",
  publish = {
    "blep",
    "foo",
    ["bar"] = {data_type = "string"},
    ["baz"] = {data_type = "integer"},
    ["floatey"] = {data_type = "float"},
    
  }
}

testmod:subscribe("core:manager.workers_started", function(self)
  assert(self:publish("blep"))
  assert(self:publish("foo", 1))
  assert(self:publish("bar", 12, "hello"))
  assert(self:publish("baz", -4, 22))
  assert(self:publish("floatey", 99, 0.121))
end)

assert(testmod:add())


local submod = Module.new {
  name= "lua_pubcheckmod",
  version = "0.0.0",
}
submod:subscribe("lua_testmod:blep", function(self, ev, code)
  assert(not self.got_blep)
  assert(code == 0)
  self.got_blep = true
end)
submod:subscribe("lua_testmod:foo", function(self, ev, code)
  assert(self.got_blep)
  assert(not self.got_foo)
  assert(not self.got_bar)
  assert(not self.got_baz)
  assert(code == 1)
  self.got_foo = true
end)
submod:subscribe("lua_testmod:foo", function(self, ev, code)
  assert(self.got_blep)
  assert(not self.got_foo)
  assert(not self.got_foo2)
  assert(not self.got_bar)
  assert(not self.got_baz)
  assert(code == 1)
  self.got_foo2 = true
end)
submod:subscribe("lua_testmod:bar", function(self, ev, code, data)
  assert(self.got_blep)
  assert(self.got_foo)
  assert(self.got_foo2)
  assert(not self.got_bar)
  assert(not self.got_baz)
  assert(code == 12)
  assert(data == "hello")
  self.got_bar = true
end)
submod:subscribe("lua_testmod:baz", function(self, ev, code, data)
  assert(self.got_blep)
  assert(self.got_foo)
  assert(self.got_foo2)
  assert(self.got_bar)
  assert(not self.got_baz)
  assert(code == -4)
  assert(data == 22)
  self.got_baz = true
end)
submod:subscribe("lua_testmod:floatey", function(self, ev, code, data)
  assert(self.got_blep)
  assert(self.got_foo)
  assert(self.got_foo2)
  assert(self.got_bar)
  assert(self.got_baz)
  assert(code == 99)
  assert(data == 0.121)
  Shuso.stop()
end)

assert(submod:add())
