local Core = require "shuttlesock.core"
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

function Event.new_event_state(evstate_ptr, full_event_name, publisher_name)
  return setmetatable({
    ptr = evstate_ptr,
    name = full_event_name,
    publisher_name = publisher_name
  }, evstate_mt)
end

return Event
