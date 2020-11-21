local testmod, field, val = ...
local Shuso = require "shuttlesock"

function testmod:initialize_config(block)
  if block.name == "::ROOT" then
    local setting = assert(block:setting("ookay"))
    assert(setting:value(1) == "1")
    assert(setting:value(2) == "2")
  end
  if block.name == "root_config" then
    local setting = assert(block:setting("bar"))
    assert(setting:value(1) == "yes")
    assert(setting:value(2) == "okay")
    assert(not setting:value(3))
  end
end

assert(testmod:add())

local config = 
[[
  bar yes okay;
  root_config hi {
    block3 {
      ookay bloop;
    }
  }
]]

assert(Shuso.configure_string("test_conf", config))
assert(Shuso:configure_finish())
