local Core = require "shuttlesock.core"
local CoreConfig = require "shuttlesock.core.config"

local Config = {}

local blocks_cache = {}
local settings_cache = {}

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
  local self = rawget(blocks_cache, ptr)
  if self then return self end
  
  local setting_ptr = Core.config_block_parent_setting_pointer(ptr)
  
  self = setmetatable({
    ptr=ptr,
    name = Core.config_setting_name(setting_ptr),
    raw_name = Core.config_setting_raw_name(setting_ptr),
    parent_setting_ptr = Core.config_block_parent_setting_pointer(ptr),
    settings_cache = {},
    contexts = setmetatable({}, block_context_mt),
    path = Core.config_block_path(ptr)
  }, block_mt)
  rawset(blocks_cache, ptr, self)
  
  return self
end

function block:parent_setting()
  --[[
  if not self.parent_setting_ptr then
    self.parent_setting_ptr = Core.config_block_parent_setting_pointer(ptr)
  end
  ]]
  assert(self.parent_setting_ptr, "block is missing parent_setting_ptr, that's really weird")
  return Config.setting(self.parent_setting_ptr)
end

function block.settings() --all local setting in this
  error("not implemented yet")
end

function block:setting(name)
  assert(type(name)=="string", "name of setting in block must be a string")
  local setting = self.settings_cache[name]
  if setting then
    return setting
  elseif setting == false then
    return nil
  end
  
  local setting_ptr = Core.config_block_setting_pointer(self.ptr, name)
  if not setting_ptr then
    self.settings_cache[name] = false
    return nil
  end
  assert(type(setting_ptr) == "userdata")
  setting = Config.setting(setting_ptr)
  
  if self.path ~= setting.path then
    --this is an inherited setting. make it so
    local inherited = setmetatable({}, getmetatable(setting))
    for k,v in pairs(setting) do
      if k == "values_cache" then
        local vals = {
          default = v.default,
          inherited = #v["local"] > 0  and v["local"] or v["inherited"],
          ["local"] = {},
          merged = v.merged
        }
        rawset(inherited, k, vals)
      else
        rawset(inherited, k, v)
      end
    end
    setting = inherited
  end
  
  self.settings_cache[name] = setting
  return setting
end

function block:context(module_name)
  if type(module_name) == "table" then
    module_name = module_name.name
  end
  assert(type(module_name) == "string", "module name isn't a string")
  return self.contexts[module_name]
end

function block:match_path(match)
  return CoreConfig.match_path(self, match)
end

function block:setting_value(name, n, data_type, value_type)
  local setting = self:setting(name)
  if not setting then
    return nil
  end
  return setting:value(n, data_type, value_type)
end

local setting = {}
local setting_mt = {
  __index = setting,
  __gxcopy_metatable = function()
    return require("shuttlesock.config").setting_metatable
  end,
  __name="config.setting"
}
function Config.setting(ptr)
  assert(type(ptr) == "userdata", "setting is a "..type(ptr).. " ".. debug.traceback())
  local self = rawget(settings_cache, ptr)
  if self then
    return self
  end
  self = setmetatable({
    name = Core.config_setting_name(ptr),
    raw_name = Core.config_setting_raw_name(ptr),
    module_name = Core.config_setting_module_name(ptr),
    path = Core.config_setting_path(ptr),
    ptr=ptr,
  }, setting_mt)
  
  self.values_cache = {}
  
  for _, vtype in ipairs{"merged", "local", "inherited", "default"} do
    local values = {}
    local valcount = Core.config_setting_values_count(ptr, vtype)
    for i=1,valcount do
      table.insert(values, assert(Core.config_setting_value(ptr, i, vtype)))
    end
    self.values_cache[vtype] = values
  end
  
  settings_cache[ptr]=self
  
  return self
end


local possible_value_types = {
  ["merged"] = true,
  ["local"] = true,
  ["inherited"] = true,
  ["default"] = true,
  ["defaults"] = true
}

local possible_data_types = {
  string = true,
  integer = true,
  number = true,
  raw = true,
  boolean = true
}

function setting:value(n, data_type, value_type)
  if type(n) == "string" and not value_type then
    n, data_type, value_type = nil, n, data_type
  end
  local val
  
  if not value_type and rawget(possible_value_types, data_type) then
    data_type, value_type = nil, data_type
  end
  if value_type == "defaults" then
    -- both are permitted because in C this is called 'defaults', but
    -- it's plural only because the singular 'default' is a reserved keyword
    value_type = "default"
  end
  
  n = n or 1
  data_type = data_type or "string"
  value_type = value_type or "merged"
  
  if not rawget(possible_data_types, data_type) then
    error("bad data type " .. tostring(data_type))
  end
  if not rawget(possible_value_types, value_type) then
    error("bad value type " .. tostring(value_type))
  end
  
  val = self.values_cache[value_type]
  if not val then
    return nil, "invalid value type' " .. tostring(value_type) .. "'"
  end
  
  val = val[n]
  if not val then
    return nil, "invalid value index " .. tostring(n)
  end
  
  val = val[data_type]
  if not val then
    return nil, "invalid value data type' " .. tostring(value_type) .. "'"
  end
  
  return val
end

function block:match_path(match)
  return CoreConfig.match_path(self, match)
end

Config.block_metatable = block_mt
Config.block_context_metatable = block_context_mt
Config.setting_metatable = setting_mt

setmetatable(Config, {
  __gxcopy_save_state = function()
    return {
      blocks = blocks_cache,
      settings = settings_cache
    }
  end,
  __gxcopy_load_state = function(data)
    blocks_cache = data.blocks
    settings_cache = data.settings
  end
})
return Config
