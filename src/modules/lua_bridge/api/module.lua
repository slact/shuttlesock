local Core = require "shuttlesock.core"
local ModuleEvent = require "shuttlesock.core.event"
local Event = require "shuttlesock.event"

local Module = {}

Module.FIRST_PRIORITY = ModuleEvent.FIRST_PRIORITY
Module.LAST_PRIORITY  = ModuleEvent.LAST_PRIORITY

local wrapped_modules = {}
local lua_modules = {}

Module.module_publish_events = {}
Module.module_subscribers = {}
Module.event_subscribers = {}

local module = {}
local module_mt = {
  __name = "module",
  __index = module,
  __gxcopy_metatable = function()
    return require("shuttlesock.module").metatable
  end,
  __gxcopy_loaded_package_directly = true
}

local function assert0(test, err)
  if not test then
    error(err, 0)
  end
  return test
end

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
      assert0(type(v) == "string", "numerically-indexed subscribe event name must be a string")
      Module.module_subscribers[self][v]={}
    elseif type(k) == "string" then
      if type(v) == "function" then
        assert0(self:subscribe(k, v))
      elseif type(v) == "table" then
        assert0(type(v.subscriber) == "function", "string-indexed subscribe event table key 'subscriber' must be function")
        assert0(self:subscribe(k, v.subscriber, v.priority))
      else
        assert0("invalid subscribe type " .. type(v)..", must be function or table", 0)
      end
    else
      assert0("invalid subscribe key type " .. type(k), 0)
    end
  end
  self.subscribe = nil
end

function Module.new(mod, version)
  if type(mod) == "string" then
    assert0(type(version) == "string", "Lua module version missing")
    mod = { name = mod, version = version }
  end
  assert0(type(mod.name) == "string", "Lua module name missing")
  assert0(type(mod) == "table")
  setmetatable(mod, module_mt)
  Module.module_subscribers[mod]={}
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
    error("expected a module name string, got " .. t, 0)
  end
  if not found then
    return nil, "module '"..tostring(name).."' not found"
  end
  return found
end

function Module.is_lua_module(tbl)
  if type(tbl)~="table" then
    return nil, "" .. type(tbl).. " instead of module table"
  elseif getmetatable(tbl) ~= module_mt then
    return nil, "module table was not created with Module.new(...)"
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
  local subs_unique = {}
  
  if lua_modules[self.name] and lua_modules[self.name] ~= self then
    error(("a different module named %s has already been added to Shuttlesock"):format(self.name), 0)
  elseif lua_modules[self.name] == self then
    return nil, "This module has already been added"
  end
  
  subscribe_to_event_names(self)
  for full_event_name, subscribers in pairs(Module.module_subscribers[self]) do
    local optional = true
    for _, sub in ipairs(subscribers) do
      local subscriber = sub.subscriber
      sub.wrapper = function (eventstate_ptr, publisher_module_name, module_name, event_name, code, data)
        assert(publisher_module_name, "publisher module name missing")
        assert(full_event_name:match(":"..event_name.."$"))
        local event = Event.new_event_state(eventstate_ptr, full_event_name, module_name)
        return true, subscriber(self, event, code, data)
      end
      table.insert(Module.event_subscribers, sub.wrapper)
      sub.index = #Module.event_subscribers
      optional = sub.optional_event_name and optional
    end
    if not subs_unique[full_event_name] then
      subs_unique[full_event_name] = true
      table.insert(subs, (optional and "~" or "") .. full_event_name)
    end
  end

  module_table.subscribe = table.concat(subs, " ")
  self.subscribe = nil
  
  if not Module.module_publish_events[self.name] then
    Module.module_publish_events[self.name] = {}
  end
  for k, v in pairs(rawget(self, "publish") or {}) do
    local name, opts
    if type(k)=="number" then
      name, opts = v, {}
    else
      assert0(type(k) == "string", "publish key isn't a number or string")
      assert0(type(v) == "table", "publish value isn't a string or table")
      name, opts = k, v
    end
    if name:match("%s") then
      error("publish name " .. name .. " can't contain spaces", 0)
    elseif name:match("%:%;") then
      error("publish name " .. name .. " can't contain punctuation other than '.'", 0)
    end
    Module.module_publish_events[self.name][name]=opts
  end
  local publish_keys = {}
  for k, _ in pairs(Module.module_publish_events[self.name] or {}) do
    table.insert(publish_keys, k)
  end
  self.publish = nil
  
  module_table.publish = table.concat(publish_keys, " ")
  local parent_modules = {}
  for _, v in ipairs(self.parent_modules or {}) do
    table.insert(parent_modules, v)
  end
  
  if #parent_modules > 0 then
    module_table.parent_modules = table.concat(parent_modules)
  end
  
  module_table.settings = self.settings
  
  lua_modules[self.name]=self
  ok, err = Core.add_module(module_table)
  if not ok then return nil, err end
  self.ptr = Core.module_pointer(self.name)
  assert(type(self.ptr) == "userdata")
  return self
end

function module:subscribe(event_name, subscriber_function, priority)
  if Core.runstate() ~= "configuring" then
    error("can't subscribe to module events while " .. Core.runstate(), 0)
  end
  if type(event_name) == "table" then
    local ret, err
    for _, evname in ipairs(event_name) do
      ret, err = self:subscribe(evname, subscriber_function, priority)
      if not ret then return nil, err end
    end
    return ret
  end
  
  assert0(type(event_name) == "string", "event name must be a string")
  assert0(type(subscriber_function) == "function", "subscriber function must be a function in case that's not perfectly clear")
  
  local optional
  optional, event_name = event_name:match("^(%~?)(.*)")
  optional = #optional > 0
  
  if lua_modules[self.name] and not Module.module_subscribers[self][event_name] then
    --module already added, but the subscribe event name wasn't declared upfront
    error(("Lua module %s can't subscribe to undeclared event %s when it's already been added to Shuttlesock"):format(self.name, event_name), 0)
  end
    
  if not Module.module_subscribers[self][event_name] then
    Module.module_subscribers[self][event_name] = {}
  end
  table.insert(Module.module_subscribers[self][event_name], {
    priority = tonumber(priority) or 0,
    subscriber = subscriber_function,
    optional_event_name = optional and ("~"..event_name) or nil
  })
  return self
end

function module:publish(event_name, code, data)
  return Core.event_publish(self.name, event_name, code, data)
end

function module:type()
  return "lua"
end

Module.metatable = module_mt
setmetatable(Module, {
  __gxcopy_save_module_state = function()
    return {
      lua_modules = lua_modules,
      module_subscribers = Module.module_subscribers,
      module_publish_events = Module.module_publish_events,
      event_subscribers = Module.event_subscribers,
      wrapped_modules = wrapped_modules,
    }
  end,
  __gxcopy_load_module_state = function(state)
    lua_modules = state.lua_modules
    Module.module_subscribers = state.module_subscribers
    Module.event_subscribers = state.event_subscribers
    Module.module_publish_events = state.module_publish_events
    wrapped_modules = state.wrapped_modules
    
  end
})
return Module
