local LazyAtomics = require "shuttlesock.core.lazy_atomics"

local Atomics = {}

local atomics_tables = setmetatable({}, {__mode = 'k'})


local function get_atomic(self, k)
  return atomics_tables[self][k]
end

local function set_atomic(self, k, val)
  return atomics_tables[self][k]:set(val)
end

local function increment_atomic(self, k, val)
  return atomics_tables[self][k]:increment(val)
end

local function destroy_atomics(self, k)
  for _, a in pairs(atomics_tables[self] or {}) do
    a:destroy()
  end
  return true
end

local atomics_mt = {
  __index = function(t, k)
    if k == "get" then
      return get_atomic
    elseif k == "set" then
      return set_atomic
    elseif k == "increment" then
      return increment_atomic
    elseif k == "destroy" then
      return destroy_atomics
    end
    local atomic = atomics_tables[t][k]
    if not atomic then
      return nil
    end
    return atomic
  end,
  __newindex = function(t, k, v)
    local atomic = atomics_tables[t][k]
    if not atomic then
      error("no atomic at key " .. tostring(k))
    end
    return atomic:set(v)
  end,
  __name="atomics",
  __gxcopy = function()
    return require("shuttlesock.atomics").metatable
  end
}

function Atomics.new(keys)
  local self = { }
  local atomics = {}
  atomics_tables[self] = atomics
  for _, k in ipairs(keys) do
    if not atomics[k] then
      atomics[k]= LazyAtomics.create()
    end
  end
  setmetatable(self, atomics_mt)
  return self
end

setmetatable(Atomics, {
  __gxcopy_save_module_state = function()
    return {
      atomics_tables = atomics_tables
    }
  end,
  __gxcopy_load_module_state = function(state)
    atomics_tables = state.atomics_tables
  end
})

Atomics.metatable = atomics_mt

return Atomics
