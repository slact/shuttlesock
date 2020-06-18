local testmod = ...
local Shuso = require "shuttlesock"

local NIL = setmetatable({}, {__name = "nil_standin", __tostring = function()return "nil" end})

testmod.variables = {
  
  
  
}


assert(testmod:add())

local config = 
[[
  ookay $testmodvar $anothertestmodvar $constvar;
]]

assert(Shuso.configure_string("test_conf", config))
assert(Shuso:configure_finish())
