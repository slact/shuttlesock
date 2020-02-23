local Module = require "shuttlesock.module"
local Watcher = require "shuttlesock.watcher"
local Process = require "shuttlesock.process"
local Shuso = require "shuttlesock"

local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0",
  
  test_initcfg_times = 0
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
    nargs="1-4",
    default_value="10 20"
  },
  { name="bar",
    path="**",
    description="anything goes",
    nargs="1-10",
    default_value="11 22 33 44",
    block=false
  },
  { name="ookay",
    path="**",
    description="anything goes",
    nargs="1-10",
    default_value="1 2 3 4",
    block=false
  }

}

testmod:subscribe("core:manager.workers_started", function()
  assert(testmod.test_initcfg_times == 4)
  Shuso.stop()
end)

function testmod:initialize_config(block)
  --print(block.setting.name, block.path)
  if block.name=="::ROOT" then
    local ctx = block:context(self)
    assert(ctx == block:context(self), "same block context twice")
    assert(block.path == "/", block.path)
    assert(block:match_path("/"))
    assert(not block:match_path("/blarg"))
    local setting = assert(block:setting("root_config"))
    assert(setting.name == "root_config")
    assert(setting:value() == "yeep")
    assert(setting:value("local") == "yeep")
    assert(setting:value(1) == "yeep")
    assert(setting:value(1, "string", "default") == "hey")
    testmod.test_initcfg_times = testmod.test_initcfg_times + 1
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
    testmod.test_initcfg_times = testmod.test_initcfg_times + 1
  elseif block.name == "block2" then
    assert(block.path == "/block1/block2")
    assert(block:setting_value("bar", 1)=="block1_1")
    assert(block:setting_value("bar", 1, "inherited")=="block1_1")
    assert(block:setting_value("bar", 1, "local")==nil)
    assert(block:setting_value("ookay", 1) == "one")
    assert(block:setting_value("ookay", 2) == "hello i am\n  two")
    assert(block:setting_value("ookay", 3) == "three")
    assert(block:setting_value("ookay", 4) == 
        "        yes this,\n        is\";~athing")
    assert(block:setting_value("ookay", 5) == 
        "        and one more\n        more")
    testmod.test_initcfg_times = testmod.test_initcfg_times + 1
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
    testmod.test_initcfg_times = testmod.test_initcfg_times + 1
  else
    error("unexpected block name "..tostring(block.name))
  end
end

assert(testmod:add())

local config = 
[[
  root_config "yeep";
  #beeep
  block1 {
    bar block1_1 block1_2;
    block2 {
      ookay one <<~TWO three <<-FOUR_THING <<FIVE; #comment
        hello i am
          two
        TWO
        yes this,
        is";~athing
        FOUR_THING
        and one more
        more
FIVE
      block3 {
        bar block3_1 block3_2;
        foo 100 150;
      }
    }
  }
]]

assert(Shuso.configure_string("test_conf", config))
assert(Shuso:configure_finish())
