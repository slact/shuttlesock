local LazyAtomics = require "shuttlesock.core.lazy_atomics"
local IPC = require "shuttlesock.ipc"

local Semaphore = {}
Semaphore.semaphores_by_id = {}

IPC.receive("shuttlesock:semaphore:signal_on", "any", function(semaphore_id, src)
  if not semaphore_id then return end
  local semaphore = Semaphore.semaphores_by_id[semaphore_id]
  if not semaphore then return end
  local waiting_list = semaphore.waiting
  semaphore.waiting = {}
  for waiting_coro, _ in pairs(waiting_list) do
    local ok, err = coroutine.resume(waiting_coro, true)
    if not ok then
      --TODO: log error maybe?
    end
  end
end)

function Semaphore.new(name)
  local self = setmetatable({name=name or "unnamed"}, Semaphore.semaphore_mt)
  self.atomic = LazyAtomics.create()
  self.atomic:set(false)
  self.id = self.atomic:get_id()
  return self
end

local semaphore = {}

function semaphore:lock()
  self.atomic:set(false)
  return self
end

function semaphore:unlock()
  self.atomic:set(true)
  IPC.send("all", "
  return self
end

Semaphore.semaphore_mt = {
  __index = semaphore,
  __gxcopy_check = function(self)
    local waiting_count = 0
    for _, _ in pairs(self.waiting) do
      waiting_count = waiting_count + 1
    end
    if waiting_count > 0 then
      return nil, ('semaphore "%s" can\'t be gxcopied with %d waiting coroutine%s'):format(self.name, waiting_count, waiting_count == 1 and "" or "s")
    end
    return true
  end
}
