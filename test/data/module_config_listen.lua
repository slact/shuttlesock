local Module = require "shuttlesock.module"
local Watcher = require "shuttlesock.watcher"
local Process = require "shuttlesock.process"
local Shuso = require "shuttlesock"

local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0",
  
  test_initcfg_times = 0
}


testmod:subscribe("core:manager.workers_started", function()
  coroutine.wrap(function()
    Watcher.timer(0.5):yield()
    Shuso.stop()
  end)()
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
assert(Shuso:configure_finish())
