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


testmod:subscribe("core:manager.workers_started", function()
  coroutine.wrap(function()
    Watcher.timer(0.1):yield()
    local io = IO.wrap("localhost:2343")()
    
    local ok, err = io:connect()
    print("YESPLEASE", ok, err)
  end)()
end)

testmod:subscribe("server:maybe_accept", function(...)
  print("MAYBE_ACCEPT", ...)
end)
testmod:subscribe("server:http.accept", function(...)
  print("ACCEPT", ...)
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
    listen 127.0.0.3:5050;
  }
}

  
]]

assert(Shuso.configure_string("test_conf", config))
