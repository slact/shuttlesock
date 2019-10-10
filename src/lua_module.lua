local LuaModule = {
  modules = {}
}
local lua_module_mt

function LuaModule.new(self)
  if type(self) == "string" then
    self = {
      name=self
    }
  elseif type(self) ~= table then
    error("parameter must be the module name or module table")
  end
  setmetatable(self, lua_module_mt)
  if LuaModule.modules[name] then
    error("Lua module "..name.." already exists")
  end
  LuaModule.modules[name] = self
  return self
end

local lua_module = {}
lua_module_mt = {
  __name = "lua_module",
  __index = lua_module,
  __gxcopy = function()
    return require("shuttlesock.lua_module").metatable
  end
}

function lua_module:subscribe(name, listener)
  assert(not self.finalized, "module already finalized")
  if not self.subscribe then
    self.subscribe = {}
  end
  assert(type(self.subscribe) == "table")
  table.insert(self.subscribe, name)
  
  if not self.listeners then
    self.listeners = {}
  end
  if not self.listeners[name] then
    self.listeners[name] = {}
  end
  table.insert(self.listeners[name], listener)
  return self
end

function lua_module:publish(name, status, data)
  if not self.finalized then
    --just setting up the 
    if not self.publish then
      self.publish = {}
    end
    assert(type(self.publish) == "table")
    
    assert(#args == 0, "can't publish event before configuration is finished")
    for _, v in ipairs(args) do
      assert(type(v)=="string")
      table.insert(self.publish, v)
    end
    return self
  else
    
  end
end
setmetatable(LuaModule, {
  __gxcopy_save_state = function()
    return {
      modules = modules
    }
  end,

  __gxcopy_load_state = function(state)
    LuaModules.modules = state.modules
  end
})

LuaModule.metatable = lua_module_mt
