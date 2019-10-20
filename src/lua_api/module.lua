local Core = require "shuttlesock.core"
local Shuttlesock = require "shuttlesock"

local wrapped_modules = {}
local lua_modules = {}
local lua_module_subscribers = setmetatable({}, {__mode = "k"})
local lua_module_publish = {}
local any_module_subscribes_to_event = {}
    
local Module = {}

local module = {}
local module_mt = {
  __name = "module",
  __index = module,
  __gxcopy_metatable = function()
    return require("shuttlesock.module").metatable
  end
}

function Module.wrap(module_name, module_ptr)
  local self = wrapped_modules[module_name]
  if not self then
    self = {ptr = module_ptr, name=module_name}
    wrapped_modules[module_name] = self
  end
  return self
end

local function subscribe_to_event_names(self)
  for k, v in pairs(rawget(self, "subscribe") or {}) do
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
  self.subscribe = nil
end

function Module.new(mod, version)
  if type(mod) == "string" then
    assert(type(version) == "string")
    mod = { name = module, version = version }
  end
  assert(type(mod) == "table")
  setmetatable(mod, module_mt)
  lua_module_subscribers[mod]={}
  lua_module_publish[mod]={}
  if mod.subscribe then
    subscribe_to_event_names(mod)
  end
  return mod
end

function Module.find(name)
  local found
  local t = type(name)
  if t == "string" then
    found = lua_modules[name]
  elseif t == "table" and t.name then
    found = lua_modules[t.name]
  else
    error("expected a module name string, got " .. t)
  end
  if not found then
    return nil, "module '"..tostring(name).."' not found"
  end
  return found
end

function Module.receive_event(publisher_module_name, module_name, event_name, code, data)
  local full_event_name = ("%s:%s"):format(publisher_module_name, event_name)
  if not any_module_subscribes_to_event[full_event_name] then
    return true
  end
  local self = assert(lua_modules[module_name])
  
  local subscribers = rawget(rawget(lua_module_subscribers, self), full_event_name)
  if not subscribers or #subscribers == 0 then
    return true
  end
  
  local publisher = Module.find(publisher_module_name)
  if not publisher then
    publisher = Module.wrap(publisher_module_name)
  end
  
  local ok, err
  for _, subscriber in ipairs(subscribers) do
    ok, err = pcall(subscriber, self, code, data, event_name, publisher)
    if not ok then
      Shuttlesock.set_error("Error receiving module event %s for module %s: %s", event_name, self.name or "?",  err or "?")
    end
  end
  
  return true
end

function module:add()
  local ok, err
  
  local module_table = {
    name = self.name,
    version = self.version,
  }
  
  local subs = {}
  subscribe_to_event_names(self)
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
  
  lua_modules[self.name]=self
  
  ok, err = Core.add_module(module_table)
  if not ok then return nil, err end
  self.ptr = Core.module_pointer(self.name)
  return self
end

function module:subscribe(event_name, listener_function)
  if Core.runstate() ~= "configuring" then
    error("can't subscribe to module events while " .. Core.runstate())
  end
  assert(type(event_name) == "string", "event name must be a string")
  assert(type(listener_function) == "function", "listener function must be a function in case that's not perfectly clear")
  if not lua_module_subscribers[self][event_name] then
    lua_module_subscribers[self][event_name] = {}
  end
  table.insert(lua_module_subscribers[self][event_name], listener_function)
  any_module_subscribes_to_event[event_name] = true
  return self
end

function module:type()
  return "lua"
end

Module.metatable = module_mt
setmetatable(Module, {
  __gxcopy_save_module_state = function()
    return {
      lua_modules = lua_modules,
      lua_module_subscribers = lua_module_subscribers,
      lua_module_publish = lua_module_publish,
      any_module_subscribes_to_event = any_module_subscribes_to_event,
      wrapped_modules = wrapped_modules
    }
  end,
  __gxcopy_load_module_state = function(state)
    lua_modules = state.lua_modules
    lua_module_subscribers = state.lua_module_subscribers
    lua_module_publish = state.lua_module_publish
    any_module_subscribes_to_event = state.any_module_subscribes_to_event
    wrapped_modules = state.wrapped_modules
  end
})
return Module
