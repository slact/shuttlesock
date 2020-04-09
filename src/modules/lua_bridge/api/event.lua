local Core = require "shuttlesock.core"
local CoreEvent = require "shuttlesock.core.event"
local Event = {}

local deferred_mt = { __index = {
  resume = function(self)
    return Core.event_resume(self.data)
  end
}}
local function new_deferral(data)
  return setmetatable({data=data}, deferred_mt)
end


local evstate_mt = {}
local evstate = {}
evstate_mt.__index = evstate
function evstate_mt.__gxcopy_metatable()
  return require("shuttlesock.event").__evstate_mt
end

function evstate:delay(reason, delay_sec)
  local data, err = Core.event_delay(self.ptr, reason, delay_sec)
  if not data then
    return nil, err
  end
  return new_deferral(data)
end

function evstate:cancel()
  local data, err = Core.event_cancel(self.ptr)
  if not data then
    return nil, err
  end
  return true
end

function evstate:pause(reason)
  local data, err = Core.event_pause(self.ptr, reason)
  if not data then
    return nil, err
  end
  return new_deferral(data)
end

function evstate:get_publisher()
  local Module = require "shuttlesock.module"
  local publisher = Module.find(self.publisher_name) or Module.wrap(self.publisher_name)
  return publisher
end

function Event.new_detached(event_init)
  local event_ptr = assert(Core.create_event())
  assert(Core.initialize_detached_event(event_init))
  local module_name = event_init.module or event_init.module_name or Core.get_active_module().name
  local event_name = event_init.name
  local event = assert(CoreEvent.get(module_name, event_name, {detached = true}))
  event.detached = true
  event.ptr = event_ptr
  assert(event:initialize({}))
  return event
end

function Event.new_event_state(evstate_ptr, full_event_name, publisher_name)
  return setmetatable({
    ptr = evstate_ptr,
    name = full_event_name,
    publisher_name = publisher_name
  }, evstate_mt)
end

local ev_mt = {
  __gxcopy_metatable = function()
    return require("shuttlesock.event").__event_mt
  end,
  __index = {
    listen = function(self, callback, priority)
      assert(type(callback) == "function")
      
    end
  }
}

Event.__evstate_mt = evstate_mt
Event.__event_mt = ev_mt
return Event
