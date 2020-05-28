local Module = require "shuttlesock.module"
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
    nargs="1-40",
    default_value="1 2 3 4",
    block=false
  },
  { name="block1",
    path="**",
    description="a block",
    nargs=0,
    block=true
  },
  { name="block2",
    path="**",
    description="a block",
    nargs=0,
    block=true
  },
  { name="block3",
    path="**",
    description="a block",
    nargs=0,
    block=true
  },
{ name="block_maybe",
    path="**",
    description="a block maybe",
    nargs="0-10",
    block="optional"
  }

}
return testmod
