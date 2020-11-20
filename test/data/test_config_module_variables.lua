local testmod, field, val = ...
local Shuso = require "shuttlesock"

local var = {
  name="testmodvar",
  description = "this is a description",
  aliases = {"foo", "bar"},
  path = "*",
  eval = function() return "meh" end
}

testmod:add_variable(var)

function testmod:initialize_config(block)
  if block.name == "::ROOT" then
    local setting = assert(block:setting("ookay"))
    assert(setting:value(1) == "meh")
  end
end

assert(testmod:add())

local config = 
[[
  ookay $testmodvar;
]]

assert(Shuso.configure_string("test_conf", config))
assert(Shuso:configure_finish())
