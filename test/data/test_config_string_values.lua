local testmod = ...
local Shuso = require "shuttlesock"

function testmod:initialize_config(block)
  --print(block.setting.name, block.path)
  local setting = assert(block:setting("ookay"))
  assert(setting:value(1) == "one")
  assert(setting:value(2) == "two")
  assert(setting:value(3) == "three has spaces")
  assert(setting:value(4) == "four ;\\;\"{}}}\"\n")
  assert(setting:value(5) == "__five__")
  assert(setting:value(6) == "6")
end

assert(testmod:add())

local config = 
[[
  ookay one two "three has spaces" "four ;\\;\"{}}}\"\n" __five__ 6;
]]

assert(Shuso.configure_string("test_conf", config))
assert(Shuso:configure_finish())
