local Core = require "shuttlesock.core"

local lua_modules = {}
local lua_module_subscribers = setmetatable({}, {__mode = "k"})
local lua_module_publish = {}
    
local Module = {}

local module = {}
local module_mt = {
  __name = "module",
  __index = module,
  __gxcopy = function()
    return require("shuttlesock.module").metatable
  end
}

function Module.new(mod, version)
  if type(mod) == "string" then
    assert(type(version) == "string")
    mod = { name = module, version = version }
  end
  assert(type(mod) == "table")
  setmetatable(mod, module_mt)
  
  lua_module_subscribers[mod]={}
  lua_module_publish[mod]={}
  return mod
end

function module:add()
  local ok, err
  
  local module_table = {
    name = self.name,
    version = self.version,
  }
  
  local subs = {}
  for k, v in pairs(self.subscribe or {}) do
    if type(k) == "number" then
      assert(type(v) == "string", "numerically-indexed subscribe event name must be a string")
      lua_module_subscribers[self][v]={}
    elseif type(k) == "string" then
      assert(type(v) == "function", "string-indexed subscribe event value must be a function")
      assert(self:subscribe(k, v))
    else
      error("invalid subscribe key type " .. type(k))
    end
  end
  
  for k, _ in pairs(lua_module_subscribers[self]) do
    table.insert(subs, k)
  end
  module_table.subscribe = table.concat(subs, " ")
  
  for _, v in ipairs(self.publish or {}) do
    lua_module_publish[self][v]=true
  end
  module_table.publish = table.concat(lua_module_publish[self], " ")
  local parent_modules = {}
  for _, v in ipairs(self.parent_modules or {}) do
    table.insert(parent_modules, v)
  end
  
  if #parent_modules > 0 then
    module_table.parent_modules = table.concat(parent_modules)
  end
  
  ok, err = Core.add_module(module_table)
  if not ok then return nil, err end
  return self
end

function module:type()
  if Module.modules[self] then
    return "lua"
  else
    return "native"
  end
end

Module.metatable = module_mt
setmetatable(Module, {
  __gxcopy_save_state = function()
    return {
      lua_modules = lua_modules,
      lua_module_subscribers = lua_module_subscribers,
      lua_module_publish = lua_module_publish
    }
  end,
  __gxcopy_load_state = function(state)
    lua_modules = state.lua_modules
    lua_module_subscribers = state.lua_module_subscribers
    lua_module_publish = state.lua_module_publish
  end
})
return Module
