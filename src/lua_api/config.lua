local Core = require "shuttlesock.core"

local Config = {}


local block_cache = {}
local block = {}
local block_mt = {
  __index = block,
  __gxcopy = function()
    return require("shuttlesock.config").block_metatable
  end,
  __name="config.block"
}
function Config.block(ptr)
  assert(type(ptr) == "userdata")
  local self = rawget(block_cache, ptr)
  if self then return self end
  self = setmetatable({ptr=ptr}, block_mt)
  block_cache[ptr]=self
  self.setting = Config.setting(Core.block_setting_pointer(ptr))
  return self
end

local setting_cache = {}
local setting = {}
local setting_mt = {
  __index = setting,
  __gxcopy = function()
    return require("shuttlesock.config").setting_metatable
  end,
  __name="config.setting"
}
function Config.setting(ptr, name, handling_module_name)
  assert(type(ptr) == "userdata")
  assert(type(name) == "string")
  local setting = rawget(setting_cache, ptr)
  if self then return self end
  self = setmetatable({ptr=ptr}, block_mt)
  self.name = name
  self.module_name = handling_module_name
  setting_cache[ptr]=self
  
  self.values = {}
  for _, vtype in ipairs{"merged", "local", "inherited", "default"} do
    local values = {}
    local valcount = Core.setting_values_count(ptr, vtype)
    for i=1,valcount do
      table.insert(values, Core.setting_value(ptr, i, vtype))
    end
    self.values[vtype] = values
  end
  
  return self
end


local possible_value_types = {merged=1, ["local"]=1, inherited=1, default=1,defaults=1}

function setting:value(n, data_type, value_type)
  assert(n, "value index is missing")
  local val
  if not value_type and possible_value_types[data_type] then
    data_type, value_type = nil, data_type
  end
  if value_type == "defaults" then
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
Config.setting_metatable = setting_mt
return Config
