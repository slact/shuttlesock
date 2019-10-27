local Module = require "shuttlesock.module"
local Watcher = require "shuttlesock.watcher"
local Process = require "shuttlesock.process"
local Shuso = require "shuttlesock"

local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0"
}

testmod.settings = {
  { name="foobar",
    path="yeet/",
    description="i dunno",
    nargs=2,
    default_value='"hey" 121',
    block=true
  },
  {name="foo2",
   path="block3/",
   description="inside block3",
   nargs="1-3",
   default_value={10,20}
  }
}

testmod:subscribe("core:manager.workers_started", function()
  Shuso.stop()
end)

function testmod:initialize_config(block)
  print("initialize config pls")
  require"mm"(block)
end

assert(testmod:add())

local config = 
[[
  block1 {
    block2 {
      block3 {
        foo2 100 150;
      }
    }
  }
]]

assert(Shuso.configure_string(config, "test.conf"))
assert(Shuso:configure_finish())
