local Module = {
  by_name = {},
  by_ptr = {}
}
local Event = {
  by_name = {}
}
Event.MODULE_EVENT_LIST_NAME_PATTERN = "([%w%_]*):([%w%_%.])"
Event.MODULE_EVENT_NAME_PATTERN = "^"..Event.MODULE_EVENT_LIST_NAME_PATTERN.."$"

local event_mt
function Event.find(module_name, event_name)
  if type(module_name)=="string" and not event_name then
    module_name, event_name = module_name:match(Event.MODULE_EVENT_NAME_PATTERN)
  end
  assert(type(module_name) == "string")
  assert(type(event_name) == "string")
  
  local event = Event.by_name[module_name..":"..event_name]
  if not event then
    return nil, "event "..mdule_name..":"..event_name.." not found"
  end
  return event
end

function Event.get(module_name, event_name)
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
  event_mt = {__index=event}
  
  function event:get_module()
    return Module.find(self.module_name)
  end
  
  function event:full_name()
    return self.module_name..":"..self.name
  end
  
  function event:add_listener(dst_module_ptr, listener_ptr, privdata_ptr)
    assert(type(dst_module_ptr) == "userdata")
    assert(type(listener_ptr) == "userdata")
    assert(type(privdata_ptr) == "userdata")
    
    local subscriber_module = Module.find(dst_module_ptr)
    if not subscriber_module then
      return nil, "unknown receiver module"
    end
    
    if self.module.finalized then
      return nil, "module "..self.module.name.." has already been finalized"
    end
    if subscriber_module.finalized then
      return nil, "module "..subscriber_module.name.." has already been finalized"
    end
    
    if not subscriber_module:depends_on_event(self:full_name()) then
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
  
  function event:initialize(init_ptr)
    assert(type(init_ptr) == "userdata")
    if self.initialized then
      return nil, "module "..self.module_name.." has already registered event "..self.name.." with a different event struct"
    end
    self.module = assert(Module.find(self.module_name))
    self.initialized = true
    self.ptr = init_ptr
    return self
  end
end


local module_mt


do
  local currently_initializing_module_name = nil
  local deps_indexed = {}
  local deps = {}
  
  function Module.start_initialization(module_name)
    if currently_initializing_module_name then
      return nil, "another module ("..currently_initializing_module_name..") is already initializing"
    end
    local ok, err = Module.find(module_name)
    if not ok then
      return ok, err
    end
    currently_initializing_module_name = module_name
    return true
  end
  function Module.finish_initialization(module_name)
    if not currently_initializing_module_name then
      return nil, "not initializing module "..module_name..", can't finish initialization"
    end
    if currently_initializing_module_name ~= module_name then
      return nil, "currently initializing module "..currently_initializing_module_name..", not "..module_name
    end
    currently_initializing_module_name = module_name
    return true
  end
  function Module.currently_initializing_module()
    if currently_initializing_module_name then
      return currently_initializing_module_name
    else
      return nil, "not currently intializing a module"
    end
  end
  
  function Module.add_dependency(provider, dependent)
    assert(type(provider) == "string")
    assert(type(dependent) == "string")
    if not deps[provider] then
      deps[provider] = {}
      deps_indexed[provider] = {}
    end
    if not deps_indexed[provider][dependent] then
      table.insert(deps[provider], dependent)
      deps_indexed[provider][dependent] = #deps[provider]
    end
    return true
  end
  
  function Module.dependency_index(provider, dependent)
    assert(type(provider) == "string")
    assert(type(dependent) == "string")
    if not deps[provider] then
      return nil, "unknown provider module "..provider
    end
    local index = deps_indexed[provider][dependent]
    if not index then
      return nil, "unknown dependent module "..dependent
    end
    return index
  end
  
  function Module.get_dependencies(provider)
    assert(type(provider) == "string")
    return deps[provider] or {}
  end
  
  function Module.each_dependency()
    return coroutine.wrap(function()
      for _, provider in pairs(deps) do
        for _, dependent in ipairs(deps[provider]) do
          coroutine.yield(provider, dependent)
        end
      end
      return nil
    end)
  end
end

function Module.find_event = Event.find

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
  assert(type(subscribe_string) == "string")
  assert(type(publish_string) == "string")
  
  if Module.find(name) or Module.find(ptr) then
    return nil, "module "..name.." has already been added"
  end
  
  local major, minor, patch = version:match("^(%d+)%.(%d+)%.(%d+)$")
  if not major then
    return nil, ('module %s has an invalid version string "%s"'):format(name, version)
  end
  
  local self = {
    name = name,
    ptr = ptr,
    version = ("%s.%s.%s"):format(major, minor, patch),
    parent_module_names = {},
    events = {
      publish = {},
      subscribe = {}
    }
  }
  setmetatable(self, module_mt)
  
  if parent_module_names_string:match("[^%s%w%_]") then
    return nil, "module "..name.." has an invalid parent_modules string. It must contain a whitespace or comma-separated list of parent module names"
  end
  
  local parent_modules = {}
  for modname in parent_module_names_string:gmatch("[%w%_]") do
    if parent_modules[modname] then
      return nil, "module "..name.." has a duplicate module name \"" .. modname ,, "\" in the parent_module string"
    end
    parent_modules[modname]=true
    table.insert(self.parent_module_names, modname)
  end
  
  if subscribe_string:match("[^%s%w%_%.%:]") then
    return nil, "module "..name.." has an invalid subscribe string. It must contain a whitespace or comma-separated list of event names of the form \"module_name:event_name"
  end
  for modname, evname in subscribe_string:gmatch(Event.MODULE_EVENT_LIST_NAME_PATTERN) do
    local mevname = modname..":"..evname
    if self.events.subscribe[mevname] then
      return nil, "module "..name.." has duplicate event \""..mevname.."\" in its subscribe list"
    end
    self.events.subscribe[mevname] = assert(Event.get(mevname))
    
    Module.add_dependency(modname, self.name)
  end
  
  if publish_string:match("[^%w%.%_%s]") then
    return nil, "module "..name.." has an invalid publish string. It must contain a whitespace-separated list of event names this module publishes, without the module prefix"
  end
  for evname in publish_string:gmatch("[%w%.%_]+") do
    if self.events.publish[evname] then
      return nil, "module "..name.." has duplicate event \""..evname.."\" in its publish list"
    end
    self.events.publish[evname] = assert(Event.get(self.name, evname))
  end
  
  Module.by_name[name]=self
  Module.by_ptr[ptr]=self
  return self
end

function Module.initialize_event(module_id, event_name, event_ptr)
  local module, event, err
  module, err = Module.find(module_id)
  if not module then return nil, err end
  
  event, err = module:event(event_name)
  if not event then return nil, err end
  
  return event:initialize(event_ptr)
end

do
  local module = {}
  module_mt = {__index = module}
  
  function module:finalize()
    if self.finalized then
      return nil, "module "..self.name.." has already been finalized"
    end
    self.parent_modules = {}
    for i, modname in ipairs(self.parent_module_names) do
      local parent = Module.find(modname)
      if not parent then
        return nil, "module "..self.name.." requires parent module "..modname..", which was not found"
      end
      self.parent_modules[i]=parent
    end
    
    module.finalized = true;
    return true
  end
  
  function module:event(name)
    assert(type(name)=="string")
    local event = self.events.publish[name]
    if not event then
      return nil, "unknown event "..name.." in module " .. self.name
    end
    return event
  end
  
  function module:dependent_modules_count()
    return #Module.get_dependencies(self.name)
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

return Module
