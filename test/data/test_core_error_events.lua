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
  Shuso.set_error("err1")
  Shuso.set_error("err2")
  Shuso.set_error("err3")
end)

local cur = 0
testmod:subscribe("core:error", function(self, code, err)
  assert(Process.type() == "manager")
  if cur == 0 then
    assert(err == "err1")
  elseif cur == 1 then
    assert(err == "err2")
  elseif cur == 2 then
    assert(err == "err3")
    Shuso.set_error("err3")
    Shuso.stop()
  end
  
  cur = cur+1
end)

assert(testmod:add())
