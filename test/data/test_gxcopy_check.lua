local Core = require "shuttlesock.core"

local ud, lud = ...

local gxcopy_check = Core.gxcopy_check

local function gxcopy_fail(val, matcherr)
  local ok, err = gxcopy_check(val)
  if ok then return false, "gxcopy_check was supposed to fail, but didn't" end
  if matcherr then
    if type(matcherr)=="string" then
      matcherr = {matcherr}
    end
    n = 1
    for line in err:gmatch("[^\n]*") do
      local matchline = matcherr[n]
      n=n+1
      if not matchline then
        return true
      end
      if not line:match(matchline) then
        return nil, "gxcopy_check error doesn't match. error: " .. err
      end
    end
    
    
  end
  return true
end

local coro = coroutine.create(function() end)
local t_with_coro={coro}
local function foo_with_coro()
  return t_with_coro
end

assert(type(ud) == "userdata")
assert(type(lud) == "userdata")
assert(not Core.is_light_userdata(ud))
assert(Core.is_light_userdata(lud))
assert(gxcopy_check("1"))
assert(gxcopy_check(1))
assert(gxcopy_check(function() end))
assert(gxcopy_check({}))
assert(gxcopy_check(lud))
assert(gxcopy_fail(ud, "userdata without metatable"))
local mt = {}
debug.setmetatable(ud, mt)
assert(gxcopy_fail(ud, "__gxcopy_load.*not a function"))

local t = {
  {
    {
      {
        {11},22
      },
      function()
        return mt
      end
    },
    [{}]="ok"
  }, 
  {
    [coro] = 11
  }
}

assert(gxcopy_fail(t, "contains a coroutine"))

local str = "string"

local function fstack()
  local up1 = lud
  local up2 = str
  local up3 = { t }
end

assert(gxcopy_fail(fstack, {"contains a coroutine", "function .* upvalue t", "table value at key", "table key"}))
