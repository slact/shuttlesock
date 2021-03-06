local Module = {
  core = nil,
  by_name = {},
  by_ptr = {},
  index_counter = 0,
  max_module_count = 255,
}
Module.dependents_indexed = {}
Module.dependents = {}
Module.providers = {}

local Core = require "shuttlesock.core"

local function split_subscribe_list_string(str)
  local Event = require "shuttlesock.core.event"
  local events = {}
  local events_required = {}
  local badchar = str:match("[^%~%s%w%_%.%:]")
  if badchar then
    return nil, nil, "invalid character '"..badchar.."' in subscribe string"
  end
  for mevname in str:gmatch("%S+") do
    if not mevname:match("^.+:.+$") then
      return nil, nil, "invalid subscribe event name \"" .. mevname .. "\" , must be modulename:eventname"
    end
    local optional, modname, evname = mevname:match("^(~?)([%w%_%.]+):([%w%_%.]+)$")
    optional = (optional == "~")
    if not modname or not evname then
      return nil, nil, "invalid subscribe event name \""..mevname.."\""
    end
    if not modname:match("^[%w%_]+$") then
      return nil, nil, "invalid module name \""..modname.."\" in subscribe event name \""..mevname.."\""
    end
    if events[mevname] then
      return nil, nil, "duplicate value \""..mevname.."\" in subscribe event name"
    end
    
    events[mevname] = Event.get(modname, evname)
    events_required[mevname] = not optional
  end
  return events, events_required
end

local function split_publish_list_string(modname, str)
  local Event = require "shuttlesock.core.event"
  local events = {}
  local badchar = str:match("[^%s%w%_%.%:]")
  if badchar then
    return nil, "invalid character '"..badchar.."' in publish string"
  end
  for evname in str:gmatch("%S+") do
    if not evname:match("^[%w%_%.]+$") then
      return nil, "invalid event name \""..evname.."\" in publish string"
    end
    if events[evname] then
      return nil, "duplicate value \""..evname.."\" in publish string"
    end
    events[evname]=Event.get(modname, evname)
  end
  return events
end
local function split_parent_modules_string(str)
  local modules = {}
  local modules_indexed = {}
  local badchar = str:match("[^%s%w%_]")
  if badchar then
    return nil, "invalid character '"..badchar.."' in parent_modules string"
  end
  for modname in str:gmatch("[%w%_]+") do
    if modules_indexed[modname] then
      return nil, "duplocate value \""..modname.."\" in parent_modules string"
    end
    modules_indexed[modname] = true
    table.insert(modules, modname)
  end
  return modules
end

local module_mt

function Module.active()
  return Core.active_module()
end

function Module.set_active(active_module)
  return Core.set_active_module(active_module)
end

function Module.add_dependency(provider_name, dependent_name)
  assert(type(provider_name) == "string")
  assert(type(dependent_name) == "string")
  if not Module.dependents[provider_name] then
    Module.dependents[provider_name] = {}
    Module.dependents_indexed[provider_name] = {}
  end
  if not Module.dependents_indexed[provider_name][dependent_name] then
    table.insert(Module.dependents[provider_name], dependent_name)
    Module.dependents_indexed[provider_name][dependent_name] = #Module.dependents[provider_name]
  end
  if not Module.providers[dependent_name] then
    Module.providers[dependent_name]={}
  end
  Module.providers[dependent_name][provider_name]=true
  return true
end

function Module.dependency_index(provider, dependent)
  assert(type(provider) == "string")
  assert(type(dependent) == "string")
  if not Module.dependents[provider] then
    return nil, "unknown provider module "..provider
  end
  local index = Module.dependents_indexed[provider][dependent]
  if not index then
    return nil, "unknown dependent module "..dependent
  end
  return index
end

function Module.get_dependencies(provider)
  assert(type(provider) == "string")
  return Module.dependents[provider] or {}
end

function Module.each_dependency()
  return coroutine.wrap(function()
    for _, provider in pairs(Module.dependents) do
      for _, dependent in ipairs(Module.dependents[provider]) do
        coroutine.yield(provider, dependent)
      end
    end
    return nil
  end)
end

function Module.count()
  return Module.index_counter
end

function Module.find(id)
  if type(id)=="userdata" then
    if not Module.by_ptr[id] then
      return nil, "no module known by that pointer"
    end
    return Module.by_ptr[id]
  elseif type(id) == "string" then
    if not Module.by_name[id] then
      return nil, "no module "..id
    end
    return Module.by_name[id]
  else
    error("don't know how ty find modules by " .. type(id) .. " type id")
  end
end

function Module.new(name, ptr, version, subscribe_string, publish_string, parent_module_names_string)
  assert(type(name) == "string")
  assert(type(ptr) == "userdata")
  assert(type(version) == "string")
  subscribe_string = subscribe_string or ""
  assert(type(subscribe_string) == "string")
  publish_string = publish_string or ""
  assert(type(publish_string) == "string")
  parent_module_names_string = parent_module_names_string or ""
  assert(type(parent_module_names_string) == "string")
  
  if Module.count() >= Module.max_module_count then
    return nil, "number of modules cannot exceed "..tonumber(Module.max_module_count)
  end
  
  if Module.find(name) or Module.find(ptr) then
    return nil, "module "..name.." has already been added"
  end
  
  local major, minor, patch = version:match("^(%d+)%.(%d+)%.(%d+)$")
  if not major then
    return nil, ('module %s has an invalid version string "%s"'):format(name, version)
  end
  
  if not Module.core and not Module.coreless then
    return nil, "core module not set"
  end
  
  
  local self = {
    name = name,
    ptr = ptr,
    version = ("%s.%s.%s"):format(major, minor, patch),
    parent_module_names = {},
    events = {
      publish = {},
      subscribe = {},
      subscribe_required = {}
    }
  }
  setmetatable(self, module_mt)
  local err
  
  self.parent_module_names, err = split_parent_modules_string(parent_module_names_string)
  if not self.parent_module_names then
    return nil, ("failed to add module %s: %s"):format(name, err)
  end
  
  self.events.subscribe, self.events.subscribe_required, err = split_subscribe_list_string(subscribe_string)
  if not self.events.subscribe then
    return nil, ("failed to add module %s: %s"):format(name, err)
  end
  
  for evname, event in pairs(self.events.subscribe) do
    if self.events.subscribe_required[evname] then
      Module.add_dependency(event.module_name, self.name)
    end
  end
  
  self.events.publish, err = split_publish_list_string(name, publish_string)
  if not self.events.publish then
    return nil, ("failed to add module %s: %s"):format(name, err)
  end
  Module.by_name[name]=self
  Module.by_ptr[ptr]=self
  Module.index_counter = Module.index_counter + 1
  self.index = Module.index_counter
  
  for _, parent in ipairs(self.parent_module_names) do
    Module.add_dependency(parent, self.name);
  end
  if Module.core then
    Module.add_dependency(Module.core.name, self.name);
  end
  
  --everyone depends on the core module
  Module.add_dependency("core", self.name)
  
  return self
end

function Module.new_core_module(name, ...)
  local err
  local module = Module.find(name)
  if module then
    return nil, "module "..name.." has already been added as a non-core module"
  end
  if Module.core then
    return nil ,"core module is already set"
  end
  Module.coreless = true
  module, err = Module.new(name, ...)
  Module.coreless = false
  if not module then
    return nil, err
  end
  Module.core = module
  return module
end

function Module.initialize_event(module_id, event_init_options)
  local module, event, err
  assert(type(module_id) == "string" or type(module_id) == "userdata")
  assert(type(event_init_options) == "table")
  assert(type(event_init_options.name) == "string")
  
  module, err = Module.find(module_id)
  if not module then return nil, err end
  
  event, err = module:event(event_init_options.name)
  if not event then return nil, err end
  
  return event:initialize(event_init_options)
end

do
  local module = {}
  module_mt = {
    __name = "module",
    __index = module,
    __gxcopy_metatable = function()
      return require("shuttlesock.core.module").metatable
    end
  }
  
  function module:freeze()
    if self.frozen then
      return true
    end
    for _, modname in ipairs(self.parent_module_names) do
      local parent = Module.find(modname)
      if not parent then
        return nil, "module "..self.name.." requires parent module "..modname..", which was not found"
      end
      Module.add_dependency(parent.name, self.name)
    end
    for event_name, event in pairs(self.events.subscribe) do
      local publishing_module = Module.find(event.module_name)
      local required = self.events.subscribe_required[event_name]
      local event_present = true
      if not publishing_module then
        event_present = false
        if required then
          return nil, ("module %s depends on event %s, but module %s was not found"):format(self.name, event_name, event.module_name)
        end
      end
      if not publishing_module:event(event.name) and not event.optional then
        event_present = false
        if required then
          return nil, ("module %s depends on event %s, but module %s does not publish such an event"):format(self.name, event_name, event.module_name)
        end
      end
      if event_present then
        Module.add_dependency(publishing_module.name, self.name)
      end
    end
    
    self.frozen = true
    return true
  end
  
  function module:create_parent_modules_index_map()
    assert(self.frozen)
    
    local map = {}
    for i=1, Module.count() do
      map[i]=0
    end
    for parent_name, _ in pairs(Module.providers[self.name]) do
      local parent = Module.find(parent_name)
      for i, child in ipairs(parent:dependent_modules()) do
        if child == self then
          map[parent.index] = i
        end
      end
      if map[parent.index] == 0 then
        return nil, "failed to create parent_modules_index_map"
      end
    end
    return map
  end
  
  function module:dependent_modules()
    local dep_names = Module.get_dependencies(self.name)
    local deps = {}
    for _, name in ipairs(dep_names) do
      table.insert(deps, assert(Module.find(name)))
    end
    return deps
  end
  
  function module:subscribes_to_event(name)
    return self.events.subscribe[name] or self.events.subscribe["~"..name]
  end
  
  function module:event(name)
    assert(type(name)=="string")
    local event = self.events.publish[name]
    if not event then
      return nil, "unknown event "..name.." in module " .. self.name
    end
    return event
  end
  
  function module:all_events_initialized()
    local uninitialized = {}
    for _, ev in pairs(self.events.publish) do
      if not ev.initialized or type(ev.ptr) ~= "userdata" then
        table.insert(uninitialized, ev.name)
      end
    end
    
    if #uninitialized == 1 then
      return nil, "module "..self.name.." has 1 uninitialized event ".. uninitialized[1]
    elseif #uninitialized > 1 then
      return nil, ("module %s has %d uninitialized events: %s"):format(self.name, #uninitialized, table.concat(uninitialized, " "))
    end
    
    return true
  end
end

setmetatable(Module, {
  __gxcopy_save_module_state = function()
    return {
      core = Module.core,
      by_name = Module.by_name,
      by_ptr = Module.by_ptr,
      index_counter = Module.index_counter,
      max_module_count = Module.max_module_count,
      dependents_indexed = Module.dependents_indexed,
      dependents = Module.dependents,
      providers = Module.providers
    }
  end,
  __gxcopy_load_module_state = function(state)
    Module.core = state.core
    Module.by_name = assert(state.by_name)
    Module.by_ptr = assert(state.by_ptr)
    Module.index_counter = assert(state.index_counter)
    Module.max_module_count = assert(state.max_module_count)
    Module.dependents_indexed = state.dependents_indexed
    Module.dependents = state.dependents
    Module.providers = state.providers
    return true
  end
})

Module.metatable = module_mt
return Module
