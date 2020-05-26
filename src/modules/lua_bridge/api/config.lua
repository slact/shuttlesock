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

function block:parent_block(parent_level)
  local parent_setting = self:parent_setting()
  if not parent_setting then
    return nil
  end
  return parent_setting:parent_block(parent_level)
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
    --this is an inherited setting. mark it as such
    local inherited_setting = setmetatable({}, getmetatable(setting))
    inherited_setting.inherited = true
    
    for k,v in pairs(setting) do
      rawset(inherited_setting, k, v)
    end
    setting = inherited_setting
  end
  
  self.settings_cache[name] = setting
  return setting
end

local function block_or_setting_error(self, ...)
  local config = Core.config_object()
  local err = config:error(config:ptr_lookup(self.ptr), ...)
  Core.set_error(err)
  return err
end

function block:error(...)
  return block_or_setting_error(self, ...)
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
    parent_block_ptr = Core.config_setting_parent_block_pointer(ptr),
    ptr=ptr,
  }, setting_mt)
  
  local block_ptr = Core.config_setting_block_pointer(ptr)
  self.block_ptr = block_ptr
  if block_ptr then
    self.block = Config.block(block_ptr)
  end
  
  return self
end


local possible_merge_types = {
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

function setting:each_value(...)
  local first, last, data_type, merge_type
  for _, v in ipairs({...}) do
    if type(v) == "number" then
      if not first then
        first = v
      elseif not last then
        last = v
      else
        error("unexpected number argument")
      end
    elseif type(v) == "string" then
      if not data_type then
        data_type = v
      elseif not merge_type then
        merge_type = v
      else
        error("unexpected string argument")
      end
    else
      error("unexpected argument type " .. type(v))
    end
  end
  
  first = first or 1
  local valcount = self:values_count(merge_type)
  if not last or last > valcount then
    last = valcount
  end
  local index = first
  return function()
    if index > last then
      return nil
    end
    local val = self:value(index, data_type, merge_type)
    index = index+1
    return index-1, val
  end
  
end

local function maybe_inherited_mergetype(self, requested_mergetype)
  if self.inherited then --this is an inherited setting, therefore the merge_type shifts over a little
    if requested_mergetype == "local" then
      --nope, no local values available, 'cause this setting was entirely inherited
      return nil, "this setting is inherited"
    elseif requested_mergetype == "inherited" and Core.config_setting_values_count(self.ptr, "local") > 0 then
      return "local"
    end
  end
  return requested_mergetype
end

function setting:values_count(merge_type)
  merge_type = merge_type or "merged"
  if not rawget(possible_merge_types, merge_type) then
    error("invalid value merge type " .. tostring(merge_type))
  end
  merge_type = maybe_inherited_mergetype(self, merge_type)
  if not merge_type then
    return 0
  end
  return assert(Core.config_setting_values_count(self.ptr, merge_type))
end

function setting:value(n, data_type, merge_type)
  if type(n) == "string" and not merge_type then
    n, data_type, merge_type = nil, n, data_type
  end
  
  if not merge_type and rawget(possible_merge_types, data_type) then
    data_type, merge_type = nil, data_type
  end
  if merge_type == "defaults" then
    -- both are permitted because in C this is called 'defaults', but
    -- it's plural only because the singular 'default' is a reserved keyword
    merge_type = "default"
  end
  
  n = n or 1
  data_type = data_type or "string"
  merge_type = merge_type or "merged"
  
  if not rawget(possible_data_types, data_type) then
    error("bad data type " .. tostring(data_type))
  end
  if not rawget(possible_merge_types, merge_type) then
    error("bad value type " .. tostring(merge_type))
  end
  
  local merge_type_err
  merge_type, merge_type_err = maybe_inherited_mergetype(self, merge_type)
  if not merge_type then
    return nil, merge_type_err
  end
  
  assert(type(self.ptr) == "userdata")
  local val, err = Core.config_setting_value(self.ptr, n, merge_type, data_type)
  if not val then
    return nil, (err or "unknown error retrieving setting value")
  end
  
  return val
end

function setting:error(...)
  return block_or_setting_error(self, ...)
end

function block:match_path(match)
  return CoreConfig.match_path(self, match)
end

function setting:parent_block(level)
  level = level or 1
  assert(self.parent_block_ptr)
  local parent_block = Config.block(self.parent_block_ptr)
  
  level = level - 1
  if level == 0 then
    return parent_block
  else
    return parent_block:parent_block(level)
  end
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
