local testmod = ...
local Shuso = require "shuttlesock"

local NIL = setmetatable({}, {__name = "nil_standin", __tostring = function()return "nil" end})

function testmod:initialize_config(block)
  --print(block.setting.name, block.path)
  local setting = assert(block:setting("ookay"))
  local check = {
    -- boolean, integer, number, size, string
    {true, NIL, NIL, NIL, "true"},
    {true, NIL, NIL, NIL, "yes"},
    {true, NIL, NIL, NIL, "on"},
    {true, 1, 1, 1, "1"},
    {false, NIL, NIL, NIL, "false"},
    {false, NIL, NIL, NIL, "no"},
    {false, NIL, NIL, NIL, "off"},
    {false, 0, 0, 0, "0"},
    
    {NIL, 11, 11.0, 11, "11"},
    {NIL, 11, 11.1, 11, "11.1"},
    {NIL, 23, 23.0, 23, "023"},
    {NIL, 16, 16, NIL, "0x10"},
    {NIL, -42, -42.0, NIL, "-42"},
    {NIL, NIL, NIL, 10547, "10.3kb"},
    {NIL, NIL, NIL, 314573, "0.3M"},
    {NIL, NIL, NIL, 1717986918, "1.6GB"}
  }
  
  local function checkval(i)
    local types = { "boolean", "integer", "number", "size", "string" }
    for checkindex, valtype in ipairs(types) do
      local val = setting:value(i, valtype)
      if val == nil then val = NIL end
      --print(i, checkindex, valtype, val, setting:value(i))
      local expected = check[i][checkindex]
      if val ~= expected then
        val = type(val) == "string" and '"'..val..'"' or val
        expected = type(expected) == "string" and '"'..expected..'"' or expected
        return nil, ("unexpected %s value for setting value #%d (%s): expected %s, got %s"):format(valtype,  i, check[i][5], tostring(expected), tostring(val))
      end
    end
    return true
  end
  
  for i=1,setting:values_count("local") do
    assert(checkval(i))
  end
end

assert(testmod:add())

local config = 
[[
  ookay true yes on 1 false no off 0 11 11.1 023 0x10 -42 10.3kb 0.3M 1.6GB;
]]

assert(Shuso.configure_string("test_conf", config))
assert(Shuso:configure_finish())
