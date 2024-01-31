local testmod = ...
local Shuso = require "shuttlesock"

function testmod:initialize_config(block)
  --print(block.setting.name, block.path)
  local setting = assert(block:setting("ookay"))
  
  assert(setting:value(1) == "FOO spacefoo")
  assert(setting:value(2) == "hellofoo")
  assert(setting:value(3) == "FOO spacefoo yehellofoos? {}")
  assert(setting:value(4) == "hellofoo-frog")
end

assert(testmod:add())

local config = 
[[
  set $hifoo hellofoo;
  set $foo FOO     spacefoo;
  set $bar "$hifoo _bar_ ${hifoo}";
  ookay $foo ${hifoo} "${foo} ye${hifoo}s? {}" ${hifoo}-frog;
]]

assert(Shuso.configure_string("test_conf", config))
assert(Shuso.configure_finish())
