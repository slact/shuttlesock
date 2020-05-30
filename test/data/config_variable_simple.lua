local testmod = ...
local Shuso = require "shuttlesock"

function testmod:initialize_config(block)
  --print(block.setting.name, block.path)
  local setting = assert(block:setting("ookay"))
  assert(setting:value(1) == "FOO")
  assert(setting:value(2) == "FOO _bar_")
  assert(setting:value(3) == "FOO what FOO _bar_ okay")
end

assert(testmod:add())

local config = 
[[
  set $foo FOO;
  set $bar "$foo _bar_";
  ookay $foo $bar "$foo what $bar okay";
]]

assert(Shuso.configure_string("test_conf", config))
assert(Shuso:configure_finish())
