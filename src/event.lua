local Event = {}

local events_by_name = {}
local events_by_ptr = {}

Event.FIRST_PRIORITY = 127
Event.LAST_PRIORITY  = -127

local event_mt
function Event.find(module_name, event_name)
  if type(module_name) == "userdata" and not event_name then --find by pointer
    local event = events_by_ptr[module_name]
    if not event then
      return nil, "event not found"
    end
    return event
  end
  
  if type(module_name)=="string" and not event_name then
    local str = module_name
    module_name, event_name = str:match("^([%w%_]+):([%w%_%.]+)$")
    if not module_name or not event_name then
      return nil, ("invalid event name \"%s\""):format(str)
    end
  end
  assert(type(module_name) == "string")
  assert(type(event_name) == "string")
  
  local event = events_by_name[module_name..":"..event_name]
  if not event then
    return nil, "event "..module_name..":"..event_name.." not found"
  end
  return event
end

function Event.get(module_name, event_name, opts)
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
  
  events_by_name[module_name..":"..event_name] = event
  return event
end
do
  local event = {}
  event_mt = {
    __name = "event",
    __index=event,
    __gxcopy_metatable = function()
      return require("shuttlesock.core.event").metatable
    end
  }
  
  function event:get_module()
    local Module = require "shuttlesock.core.module"
    return Module.find(self.module_name)
  end
  
  function event:full_name()
    return self.module_name..":"..self.name
  end
  
  function event:add_listener(dst_module_ptr, listener_ptr, privdata_ptr, priority, optional)
    assert(type(dst_module_ptr) == "userdata")
    assert(type(listener_ptr) == "userdata")
    assert(type(privdata_ptr) == "userdata")
    assert(type(priority) == "number")
    if priority < Event.LAST_PRIORITY then
      error("listener priority cannot be below " .. tostring(Event.LAST_PRIORITY))
    end
    if priority > Event.FIRST_PRIORITY then
      error("listener priority cannot be above " .. tostring(Event.FIRST_PRIORITY))
    end
    
    local Module = require "shuttlesock.core.module"
    
    local subscriber_module = Module.find(dst_module_ptr)
    if not subscriber_module then
      return nil, "unknown receiver module"
    end
    
    if not self.module then
      return nil, "module "..subscriber_module.name.." tried to listen to nonexistent event "..self:full_name()
    end
    
    if self.module.finalized then
      return nil, ("module %s can't add event listener for event '%s': module %s has already been finalized"):format(subscriber_module.name, self:full_name(), self.module.name)
    end
    if subscriber_module.finalized then
      return nil, ("module %s can't add event listener for event '%s': module %s has already been finalized"):format(subscriber_module.name, self:full_name(), subscriber_module.name)
    end
    if not subscriber_module:subscribes_to_event(self:full_name()) then
      return nil, "module ".. subscriber_module.name.." has not declared event ".. self:full_name() .. " in its 'subscribe' list, so it cannot be used."
    end
    
    local listener = {
      module = subscriber_module,
      listener = listener_ptr,
      privdata = privdata_ptr,
      priority = priority,
      optional = optional
    }
    
    if priority == Event.LAST_PRIORITY then
      table.insert(self.listeners, listener)
    elseif priority == Event.FIRST_PRIORITY then
      table.insert(self.listeners, 1, listener)
    else
      local inserted
      for i, v in ipairs(self.listeners) do
        if priority >= v.priority then
          table.insert(self.listeners, i, listener)
          inserted = true
          break
        end
      end
      if not inserted then
        table.insert(self.listeners, #self.listeners+1, listener)
      end
    end
    
    if self.optional == nil and listener.optional then
      self.optional = true
    elseif self.optional and not listener.optional then
      self.optional = false
    end
    
    return listener
  end
  
  function event:initialize(opts)
    local Module = require "shuttlesock.core.module"
    if self.initialized then
      return nil, "module "..self.module_name.." has already registered event "..self.name.." with a different event struct"
    end
    
    assert(type(opts.ptr) == "userdata", "event ptr must be a userdata")
    
    self.module = assert(Module.find(self.module_name))
    self.initialized = true
    for k, v in pairs(opts) do
      self[k]=v
    end
    self.cancelable = self.cancelable or false
    self.pausable = self.pausable or false
    
    events_by_ptr[self.ptr] = self
    
    return self
  end
end

Event.metatable = event_mt

setmetatable(Event, {
  __gxcopy_save_module_state = function()
    local k = {
      by_name = events_by_name,
      by_ptr = events_by_ptr
    }
    return k
  end,
  __gxcopy_load_module_state = function(state)
    events_by_name = assert(state.by_name, "by_name missing from global Event state")
    events_by_ptr = assert(state.by_ptr, "by_ptr missing from global Event state")
  end
})

return Event
