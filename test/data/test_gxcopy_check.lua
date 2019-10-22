local Core = require "shuttlesock.core"

local ud, lud = ...

local gxcopy_check = Core.gxcopy_check

local function gxcopy_fail(val, matcherr)
  local ok, err = gxcopy_check(val)
  if ok then return false, "gxcopy_check was supposed to fail, but didn't" end
  if matcherr then
    local oneline = false
    if type(matcherr)=="string" then
      matcherr = {matcherr}
      oneline = true
    end
    local n = 0
    for line in err:gmatch("[^\n]*") do
      n=n+1
      local matchline = matcherr[n]
      if not matchline then
        if oneline then
          return true
        else
          return nil, "not enough lines in gxcopy_check error: " .. err
        end
      end
      if not line:match(matchline) then
        return nil, "gxcopy_check error doesn't match. error: " .. err
      end
    end
    if not oneline and n ~= #matcherr then
      return nil, "too many lines in gxcopy_check error: " .. err
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


t={{{22}}}
t.t=t
assert(gxcopy_check(t))

setmetatable(t, {
  foo=coro
})
assert(gxcopy_fail(t, {"coroutine", "table metatable value at key.*foo"}))

setmetatable(t, {
  __gxcopy_save = false,
  __gxcopy_load = function()end,
})
assert(gxcopy_fail(t, {"__gxcopy_save.*not a function"}))


setmetatable(t, {
  __gxcopy_save = function()end,
  __gxcopy_load = "99"
})
assert(gxcopy_fail(t, {"__gxcopy_load.*not a function"}))

local mt = {
  __gxcopy_metatable={}
}
setmetatable(t, mt)
assert(gxcopy_fail(t, {"__gxcopy_metatable.*not a function", "in table"}))

local mt = {
  __gxcopy_metatable=function()
    return {}
  end
}
setmetatable(t, mt)
assert(gxcopy_fail(t, {"__gxcopy_metatable.*exact same metatable", "in table"}))

mt.__gxcopy_metatable=function()
  return mt
end

assert(gxcopy_fail(t, {"__gxcopy_metatable.*upvalue", "in table"}))

_G['globmeta']=mt
mt.__gxcopy_metatable=function()
  return globmeta
end

assert(gxcopy_check(t))
