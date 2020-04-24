local Module = require "shuttlesock.module"
local Watcher = require "shuttlesock.watcher"
local Process = require "shuttlesock.process"
local Shuso = require "shuttlesock"
local IO = require "shuttlesock.io"
local Log = require "shuttlesock.log"

local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0",
  
  test_initcfg_times = 0
}


testmod:subscribe("server:manager.start", function()
  coroutine.wrap(function()
    local io = assert(IO.wrap("localhost:21595")())
    assert(io:connect())
    assert(io:write("oh hello there"))
    local str = assert(io:read_partial(20))
    assert(str == "yes hello to you too")
    assert(io:wait("r"))
  end)()
end)

testmod:subscribe("server:maybe_accept", function(self, event, rc, data)
  --okay
end)

testmod:subscribe("server:http.accept", function(self, event, rc, data)
  IO.wrap(data.socket, function(io)
    local str = assert(io:read_partial(20))
    assert(str == "oh hello there")
    assert(io:write("yes hello to you too"))
    assert(io:close())
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
    listen 127.0.0.1:5050;
  }
}

  
]]

assert(Shuso.configure_string("test_conf", config))
