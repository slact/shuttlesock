local core = require "shuttlesock.core"

local Mutex = {
  metatable = {
    __index = {},
    __name = "pthread_mutex",
    __gxcopy_metatable = function()
      return require("shuttlesock.debug.mutex").metatable
    end
  }
}
local mutex = Mutex.metatable.__index

function Mutex.new()
  local lock = core.mutex_create()
  return setmetatable({mutex_ptr = lock}, Mutex.metatable)
end

function mutex:lock()
  assert(type(self.mutex_ptr)=="userdata", "mutex already destroyed")
  local ok, err = core.mutex_lock(self.mutex_ptr)
  if not ok then return nil, err end
  return self
end
function mutex:trylock()
  assert(type(self.mutex_ptr)=="userdata", "mutex already destroyed")
  local ok, err = core.mutex_trylock(self.mutex_ptr)
  if not ok then return nil, err end
  return self
end
function mutex:unlock()
  assert(type(self.mutex_ptr)=="userdata", "mutex already destroyed")
  local ok, err = core.mutex_unlock(self.mutex_ptr)
  if not ok then return nil, err end
  return self
end
function mutex:destroy()
  assert(type(self.mutex_ptr)=="userdata", "mutex already destroyed")
  local ok, err = core.mutex_destroy(self.mutex_ptr)
  if not ok then return nil, err end
  return self
end

return Mutex
