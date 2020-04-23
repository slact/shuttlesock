local Module = require "shuttlesock.module"
local Watcher = require "shuttlesock.watcher"
local Process = require "shuttlesock.process"
local Shuso = require "shuttlesock"
local IO = require "shuttlesock.io"

local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0",
  
  test_initcfg_times = 0
}


testmod:subscribe("server:manager.start", function()
  coroutine.wrap(function()
    local io = assert(IO.wrap("localhost:21595")())
    assert(io:connect())
  end)()
end)

testmod:subscribe("server:maybe_accept", function(self, event, rc, data)
  print("MAYBE_ACCEPT")
  require"mm"(event)
end)

testmod:subscribe("server:http.accept", function(self, event, rc, data)
  print("ACCEPT")
  require"mm"(event)
end)

function testmod:initialize_config(block)
  --print(block.setting.name, block.path)
  
end

assert(testmod:add())

local config = 
[[
http {
  server {
    listen localhost:11595;
  }
  server {
    listen localhost:11595;
  }
  server {
    listen 127.0.0.1:21595;
  }
}
 
stream {
  server {
    listen 127.0.0.1:5050;
  }
}

  
]]

assert(Shuso.configure_string("test_conf", config))
