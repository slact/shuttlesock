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
  { name="root_config",
    path="/",
    description="hmm",
    nargs=1,
    default_value='"hey"',
    block=true
  },
  {name="foo",
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
  --print(block.setting.name, block.path)
  if block.name=="::ROOT" then
    assert(block.path == "/", block.path)
  elseif block.name=="block1" then
    assert(block.path == "/block1", block.path)
  elseif block.name == "block2" then
    assert(block.path == "/block1/block2")
  elseif block.name == "block3" then
    assert(block.path == "/block1/block2/block3")
  else
    error("unexpected block name "..tostring(block.name))
  end
end

assert(testmod:add())

local config = 
[[
  block1 {
    block2 {
      block3 {
        foo 100 150;
      }
    }
  }
]]

assert(Shuso.configure_string(config, "test.conf"))
assert(Shuso:configure_finish())
