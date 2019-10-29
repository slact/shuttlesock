local Module = require "shuttlesock.module"
local Watcher = require "shuttlesock.watcher"
local Process = require "shuttlesock.process"
local Shuso = require "shuttlesock"

local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0",
}

local coro = coroutine.create(function() end)
local t = setmetatable({}, {__coro=function() return coro end})

function testmod:initialize()
  self.bad = t
end

assert(testmod:add())
