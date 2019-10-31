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
  { name="foo",
    path="block3/",
    description="inside block3",
    nargs="1-3",
    default_value="10 20"
  },
  { name="bar",
    path="**",
    description="anything goes",
    nargs="1-10",
    default_value="11 22 33 44",
    block=false
  }
}

testmod:subscribe("core:manager.workers_started", function()
  Shuso.stop()
end)

function testmod:initialize_config(block)
  --print(block.setting.name, block.path)
  if block.name=="::ROOT" then
    assert(block.path == "/", block.path)
    assert(block:match_path("/"))
    assert(not block:match_path("/blarg"))
    local setting = block:setting("root_config")
    assert(setting.name == "root_config")
    assert(setting:value() == "yeep")
    assert(setting:value("local") == "yeep")
    assert(setting:value(1) == "yeep")
    assert(setting:value(1, "string", "default") == "hey")
  elseif block.name=="block1" then
    assert(block.path == "/block1", block.path)
    assert(block:match_path("block1/**"))
    assert(not block:setting("root_config")) -- out of scope
    local setting = block:setting("bar")
    assert(block:setting("bar"):value(1, "default")=="11")
    assert(block:setting("bar"):value(4, "default")=="44")
    assert(block:setting("bar"):value(1, "inherited")==nil)
    assert(block:setting_value("bar", 2)=="block1_2")
    assert(block:setting_value("bar", 3)==nil)
  elseif block.name == "block2" then
    assert(block.path == "/block1/block2")
    assert(block:setting_value("bar", 1)=="block1_1")
    assert(block:setting_value("bar", 1, "inherited")=="block1_1")
    assert(block:setting_value("bar", 1, "local")==nil)
  elseif block.name == "block3" then
    assert(block.path == "/block1/block2/block3")
    local setting = block:setting("bar")
    assert(setting)
    assert(setting:value(1, "string") == "block3_1")
    assert(setting:value(2, "string") == "block3_2")
    
    local foo = block:setting("foo")
    assert(foo:value(1, "string") == "100")
    local numval = foo:value(1, "number")
    assert(math.type(numval) == "float")
    assert(numval == 100)
    local intval = foo:value(1, "integer")
    assert(math.type(intval) == "integer")
  else
    error("unexpected block name "..tostring(block.name))
  end
end

assert(testmod:add())

local config = 
[[
  root_config "yeep";
  block1 {
    bar block1_1 block1_2;
    block2 {
      block3 {
        bar block3_1 block3_2;
        foo 100 150;
      }
    }
  }
]]

assert(Shuso.configure_string(config, "test.conf"))
assert(Shuso:configure_finish())
