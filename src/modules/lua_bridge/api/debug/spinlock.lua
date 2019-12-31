local core = require "shuttlesock.core"

local Spinlock = {
  metatable = {
    __index = {},
    __name = "pthread_spinlock",
    __gxcopy_metatable = function()
      return require("shuttlesock.debug.spinlock").metatable
    end
  }
}
local spinlock = Spinlock.metatable.__index

function Spinlock.new()
  local lock = core.pthread_spinlock_create()
  return setmetatable({spinlock_ptr = lock}, Spinlock.metatable)
end

function spinlock:lock()
  assert(type(self.spinlock_ptr)=="userdata", "spinlock already destroyed")
  local ok, err = core.pthread_spinlock_lock(self.spinlock_ptr)
  if not ok then return nil, err end
  return self
end
function spinlock:trylock()
  assert(type(self.spinlock_ptr)=="userdata", "spinlock already destroyed")
  local ok, err = core.pthread_spinlock_trylock(self.spinlock_ptr)
  if not ok then return nil, err end
  return self
end
function spinlock:unlock()
  assert(type(self.spinlock_ptr)=="userdata", "spinlock already destroyed")
  local ok, err = core.pthread_spinlock_unlock(self.spinlock_ptr)
  if not ok then return nil, err end
  return self
end
function spinlock:destroy()
  assert(type(self.spinlock_ptr)=="userdata", "spinlock already destroyed")
  local ok, err = core.pthread_spinlock_destroy(self.spinlock_ptr)
  if not ok then return nil, err end
  return self
end

return Spinlock
