local LuaModule = {
  modules = {},
  modules_indexed_by_table = setmetatable({}, {__mode='kv'})
}

function LuaModule.find(key)
  local t = type(key)
  if t =="string" then
    return LuaModule.modules[key]
  elseif t == "table" then
    return LuaModule.modules_indexed_by_table[key] or LuaModule.modules[key.name]
  else
    error("bad LuaModule key")
  end
end

function LuaModule.new(module)
  assert(type(module) == "table")
  assert(type(module.name) == "string")
  local self = {
    name = module.name,
    module = module,
    subscribe = {},
    subscribe_indexed = {},
    publish = {},
    publish_indexed = {},
    event_listeners = {}
  }
  if LuaModule.modules[self.name] then
    error("Lua module "..self.name.." already exists")
  end
  LuaModule.modules[self.name] = self
  LuaModule.modules_indexed_by_table[self] = self
  LuaModule.modules_indexed_by_table[module] = self
  return self
end

function LuaModule.receive_event(module_name, event_name, publisher, data)
  local self = rawget(LuaModule.modules, module_name)
  if not self then
    return nil, "module "..module_name.." not found"
  end
  
  for _, listener in ipairs(rawget(self.event_listeners, event_name)) do
    listener(self.module, event_name, publisher, data)
  end
  return true
end

function LuaModule.subscribe(module, event_name, listener)
  local self = LuaModule.find(module)
  assert(self, "module not found")
  
  if self.subscribe_indexed[self.name] then
    return nil, "already listening for event " .. self.name
  end
  assert(type(self.subscribe) == "table")
  table.insert(self.subscribe, self.name)
  

  if not self.event_listeners[self.name] then
    self.event_listeners[self.name] = {}
  end
  table.insert(self.event_listeners[self.name], listener)
  return true
end

function LuaModule.publish(module, event_name)
  assert(type(event_name) == "string")
  local self = LuaModule.find(module)
  assert(not self.finalized, "module is already finalized")
  
  if not self.publish_indexed[event_name] then
    table.insert(self.publish, event_name)
    self.publish_indexed[event_name]=true
  end
  return true
end

setmetatable(LuaModule, {
  __gxcopy_save_state = function()
    return {
      modules = LuaModule.modules,
      modules_indexed_by_table = LuaModule.modules_indexed_by_table
    }
  end,

  __gxcopy_load_state = function(state)
    LuaModule.modules = state.modules
    LuaModule.modules_indexed_by_table = state.modules_indexed_by_table
    return true
  end
})

return LuaModule
