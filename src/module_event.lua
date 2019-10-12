local Event = {
  by_name = {},
  data_type_map = {}
}

local event_mt
function Event.find(module_name, event_name)
  if type(module_name)=="string" and not event_name then
    local str = module_name
    module_name, event_name = str:match("^([%w%_]+):([%w%_%.]+)$")
    if not module_name or not event_name then
      return nil, ("invalid event name \"%s\""):format(str)
    end
  end
  assert(type(module_name) == "string")
  assert(type(event_name) == "string")
  
  local event = Event.by_name[module_name..":"..event_name]
  if not event then
    return nil, "event "..module_name..":"..event_name.." not found"
  end
  return event
end

function Event.get(module_name, event_name)
  if type(module_name)=="string" and not event_name then
    module_name, event_name = module_name:match(Event.MODULE_EVENT_NAME_PATTERN)
  end
  local event = Event.find(module_name, event_name)
  if event then
    return event
  end
  
  event = {
    name = event_name,
    module_name = module_name,
    listeners = {}
  }
  setmetatable(event, event_mt)
  
  Event.by_name[module_name..":"..event_name] = event
  return event
end
do
  local event = {}
  event_mt = {
    __name = "event",
    __index=event,
    __gxcopy = function()
      return require("shuttlesock.core.module_event").metatable
    end
  }
  
  function event:get_module()
    local Module = require "shuttlesock.core.module"
    return Module.find(self.module_name)
  end
  
  function event:full_name()
    return self.module_name..":"..self.name
  end
  
  function event:add_listener(dst_module_ptr, listener_ptr, privdata_ptr)
    assert(type(dst_module_ptr) == "userdata")
    assert(type(listener_ptr) == "userdata")
    assert(type(privdata_ptr) == "userdata")
    local Module = require "shuttlesock.core.module"
    
    local subscriber_module = Module.find(dst_module_ptr)
    if not subscriber_module then
      return nil, "unknown receiver module"
    end
    
    if not self.module then
      return nil, "module "..subscriber_module.name.." tried to listen to nonexistent event "..self:full_name()
    end
    
    if self.module.finalized then
      return nil, "module "..self.module.name.." has already been finalized"
    end
    if subscriber_module.finalized then
      return nil, "module "..subscriber_module.name.." has already been finalized"
    end
    if not subscriber_module:subscribes_to_event(self:full_name()) then
      return nil, "module ".. subscriber_module.name.." has not declared event ".. self:full_name() .. " in its 'subscribe' list, so it cannot be used."
    end
    
    local listener = {
      module = subscriber_module,
      listener = listener_ptr,
      privdata = privdata_ptr
    }
    
    table.insert(self.listeners, listener)
    return listener
  end
  
  function event:initialize(init_ptr, data_type)
    local Module = require "shuttlesock.core.module"
    assert(type(init_ptr) == "userdata")
    if self.initialized then
      return nil, "module "..self.module_name.." has already registered event "..self.name.." with a different event struct"
    end
    self.module = assert(Module.find(self.module_name))
    self.initialized = true
    self.ptr = init_ptr
    self.data_type = data_type
    return self
  end
end

Event.metatable = event_mt

function Event.register_data_type(language, data_type_name, registering_module_name, callbacks_ptr)
  assert(type(language)=="string")
  assert(type(data_type_name)=="string")
  assert(type(callbacks_ptr)=="userdata")
  assert(type(registering_module_name) == "string")
  
  if not Event.data_type_map[data_type_name] then
    Event.data_type_map[data_type_name] = {}
  end
  local map = Event.data_type_map[data_type_name]
  if not map[language] then
    map[language] = {}
  end
  if map[language] then
    return nil, ("%s event data type %s is already registered by module '%s'"):format(language, data_type_name, registering_module_name)
  end
  map[language]=callbacks_ptr
  return true
end

function Event.data_type_map(language, data_type_name)
  if not Event.data_type_map[data_type_name] then
    return nil, "unknown event data type " .. data_type_name
  end
  local mapping = Event.data_type_map[data_type_name][language]
  if not mapping then
    return nil, ("no mapping in event data type %s for language %s"):format(data_type_name. language)
  end
  assert(type(mapping)=="userdata")
  return mapping
end

setmetatable(Event, {
  __gxcopy_save_state = function()
    return {
      by_name = Event.by_name,
      data_type_map = Event.data_type_map
    }
  end,
  __gxcopy_load_state = function(state)
    Event.by_name = assert(state.by_name, "by_name missing from global Event state")
    Event.data_type_map = assert(state.data_type_map, "data_type_map missing from global Event state")
    return true
  end
})

return Event
