local Core = require "shuttlesock.core"

local Config = {}

local blocks = {}

local block = {}
local block_mt = {
  __index = block,
  __gxcopy_metatable = function()
    return require("shuttlesock.config").block_metatable
  end,
  __name="config.block"
}

local block_context_mt = {
  __mode="k",
  __gxcopy_metatable = function()
    return require("shuttlesock.config").block_context_metatable
  end,
  __newindex = function(t, k)
    local ctx = {}
    rawset(t, k, ctx)
    return ctx
  end
}

function Config.block(ptr)
  assert(type(ptr) == "userdata")
  local self = rawget(blocks, ptr)
  if self then return self end
  self = setmetatable({ptr=ptr}, block_mt)
  rawset(blocks, ptr, self)
  
  self.parent_setting_ptr = Core.config_block_parent_setting_pointer(ptr)
  self.settings = {}
  self.contexts = setmetatable({}, block_context_mt)
  return self
end

function block:setting(name)
  local setting = self.settings[name]
  if setting then
    return setting
  elseif setting == false then
    return nil
  end
  
  local setting_ptr = Core.block_setting_pointer(self.ptr, name)
  if not setting_ptr then
    self.settings[name] = false
    return nil
  end
  setting = Config.setting(setting_ptr)
  self.settings[name] = setting
  return setting
end

function block:context(module_name)
  if type(module_name) == "table" then
    module_name = module_name.name
  end
  return self.contexts[module_name]
end

function block:setting_value(name, n, data_type, value_type)
  local setting = block:setting(name)
  if not setting then
    return nil
  end
  return setting:value(n, data_type, value_type)
end



local setting_cache = {}
local setting = {}
local setting_mt = {
  __index = setting,
  __gxcopy_metatable = function()
    return require("shuttlesock.config").setting_metatable
  end,
  __name="config.setting"
}
function Config.setting(ptr)
  assert(type(ptr) == "userdata")
  local self = rawget(setting_cache, ptr)
  if self then
    return self
  end
  self = setmetatable({
    name = Core.config_setting_name(ptr),
    raw_name = Core.config_setting_raw_name(ptr),
    module_name = Core.setting.config_module_name(ptr),
    ptr=ptr
  }, block_mt)
  setting_cache[ptr]=self
  
  self.values = {}
  for _, vtype in ipairs{"merged", "local", "inherited", "default"} do
    local values = {}
    local valcount = Core.config_setting_values_count(ptr, vtype)
    for i=1,valcount do
      table.insert(values, Core.config_setting_value(ptr, i, vtype))
    end
    self.values[vtype] = values
  end
  
  return self
end


local possible_value_types = {
  ["merged"] = true,
  ["local"] = true,
  ["inherited"] = true,
  ["default"] = true,
  ["defaults"] = true
}

function setting:value(n, data_type, value_type)
  if type(n) == "string" and not value_type then
    n, data_type, value_type = 1, n, data_type
  end
  assert(n, "value index is missing")
  local val
  if not value_type and rawget(possible_value_types, data_type) then
    data_type, value_type = nil, data_type
  end
  if value_type == "defaults" or not value_type then
    -- both are permitted because in C this is called 'defaults', but
    -- it's plural only because the singular 'default' is a reserved keyword
    value_type = "default"
  end
  
  val = self.values[value_type]
  if not val then
    return nil, "invalid value type' " .. tostring(value_type) .. "'"
  end
  
  val = val[n]
  if not val then
    return nil, "invalid value index " .. tostring(n)
  end
  
  val = val[data_type or "string"]
  if not val then
    return nil, "invalid value data type' " .. tostring(value_type) .. "'"
  end
  
  return val
end

Config.block_metatable = block_mt
Config.block_context_metatable = block_context_mt
Config.setting_metatable = setting_mt

setmetatable(Config, {
  __gxcopy_save_state = function()
    return {
      blocks = blocks,
    }
  end,
  __gxcopy_load_state = function(data)
    blocks = data.blocks
  end
})
return Config
