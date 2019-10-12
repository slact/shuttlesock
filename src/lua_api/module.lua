local Core = require "shuttlesock.core"
local Module = {}

local module = {}
local module_mt = {
  __name = "module",
  __index = module,
  __gxcopy = function()
    return require("shuttlesock.module").metatable
  end
}

function Module.new(mod)
  if type(mod) == "string" then
    mod = { name = module }
  end
  assert(type(mod) == "table")
  setmetatable(mod, module_mt)
  return mod
end

function Module.add(mod)
  local ok, err
  ok, err = Core.add_module(mod)
  if not ok then return nil, err end
  return Core.add_module(mod)
end

function module:add()
  return Module.add(self)
end

Module.metatable = module_mt
return Module
