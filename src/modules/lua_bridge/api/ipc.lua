local Core = require "shuttlesock.core"
local Shuso = require "shuttlesock"
local Process = require "shuttlesock.process"
local IPC = {}
--local Log = require "shuttlesock.log"


IPC.code = 0
IPC.reply_code = 0

--[[ receivers are not copied from manager to workers, and must be re-initialized
at the start of each worker. this is to permit the use of coroutine-based
receivers, which cannot be gxcopied between independent Lua states
]]
local receivers = setmetatable({}, {
  __index = function(t, k)
    local tt = {}
    rawset(t, k, tt)
    return tt
  end,
  __newindex = function(t, k)
    error("nope")
  end
})

local function validate_process_direction(process, direction)
  local ptype = type(process)
  local many = false
  if ptype == "number" then
    if not Core.procnum_valid(process) then
      error("invalid "..(direction or "").." procnum")
    end
  elseif not process then
    if direction == "receive" then
      process = "any"
    else
      error("missing "..(direction or "").." procnum")
    end
  elseif process == "all" and direction == "send" then
    process = "all"
    many = true
  elseif process == "others" and direction == "send" then
    process = "others"
    many = true
  elseif process == "workers" and direction == "send" then
    process = "workers"
    many = true
  elseif process == "other_workers" and direction == "send" then
    process = "other_workers"
    many = true
  elseif process == "master" then
    process = -2
  elseif process == "manager" then
    process = -1
  else
    error("invalid "..(direction or "").." procnum")
  end
  return process, many
end

local function ipc_broadcast(dst, name, data, handler, must_yield)
  local my_procnum = dst == "others" or dst == "other_workers" and Process.procnum()
  local min_procnum = dst == ("workers" or dst == "other_workers") and 0 or -math.huge
  local dstprocnums = {}
  for _, procnum in ipairs(Process.all_procnums()) do
    if procnum ~= my_procnum and procnum >= min_procnum then
      table.insert(dstprocnums, procnum)
    end
  end
  local need_shmem = not Process.share_heap(Process.procnum(), dstprocnums)
  
  local packed_data = assert(Core.ipc_pack_message_data(data, name, need_shmem))
  
  local responses_pending, failed_count = #dstprocnums, 0
  local ipc_send_acknowledged_handler_coro = coroutine.wrap(function(success)
    while true do
      responses_pending = responses_pending - 1
      if not success then
        failed_count = failed_count + 1
      end
      if responses_pending == 0 then
        break
      end
      success = coroutine.yield()
    end
    
    assert(Core.ipc_gc_message_data(packed_data))
    
    if type(handler) == "function" then
      return handler(failed_count == 0)
    elseif type(handler) == "thread" then
      return coroutine.resume(handler, failed_count == 0)
    end
  end)
  for _, procnum in ipairs(dstprocnums) do
    local ok = Core.ipc_send_message(procnum, nil, nil, packed_data, ipc_send_acknowledged_handler_coro)
    assert(ok)
  end
  
  if must_yield then
    return coroutine.yield()
  else
    return true
  end
end

function IPC.send(destination, name, data, how_to_handle_acknowledgement)
  local dst, many_dsts = validate_process_direction(destination, "send")
  assert(type(name)=="string", "invalid argument #2 to IPC.send")
  local handler, must_yield
  
  if how_to_handle_acknowledgement == nil then
    how_to_handle_acknowledgement = coroutine.isyieldable() and "yield" or "noyield"
  end
  
  if how_to_handle_acknowledgement == "yield" then
    assert(coroutine.isyieldable(), "can't send IPC message with 'yield' option from outisde a yieldable coroutine")
    handler = coroutine.running()
    must_yield = true
  elseif type(how_to_handle_acknowledgement) == "function" or type(how_to_handle_acknowledgement) == "thread" then
    handler = how_to_handle_acknowledgement
  end
  
  if many_dsts then
    return ipc_broadcast(destination, name, data, handler, must_yield)
  else
    local ok = Core.ipc_send_message(dst, name, data, nil, handler)
    if must_yield then
      return coroutine.yield()
    else
      return ok
    end
  end
end

function IPC.receive(name, src, receiver, timeout)
  --flexible args. looks ugly, but works good.
  local srctype = type(src)
  if srctype == "function" or srctype == "thread" then
    assert(timeout == nil, "invalid arguments to IPC.receive")
    src, receiver, timeout = "any", src, receiver
  end
  if type(receiver) == "number" then
    assert(timeout == nil, "invalid arguments to IPC.receive")
    receiver, timeout = nil, receiver
  end
  if timeout then
    error("Timeouts for IPC receivers not yet implemented")
  end
  src = src or "any"
  if type(receiver) == "function" then
    assert(type(receiver) == "function", "expected receiver argument to be a function")
    receivers[name][receiver]=src
    return true
  else
    if not receiver then
      assert(coroutine.isyieldable(), "can't receive IPC message from outisde a yieldable coroutine")
      receiver = coroutine.running()
    end
    
    receivers[name][receiver]=src
    return coroutine.yield()
  end
end

local receiver_mt
IPC.Receiver = {}
function IPC.Receiver.new(name, src)
  local self = {buffer = {}, oldest=1, newest=0, name = name, src = src}
  setmetatable(self, receiver_mt)
  return self
end

function IPC.Receiver.start(name, src)
  return IPC.Receiver.new(name, src):start()
end

receiver_mt = {
  __index = {
    yield = function(self)
      local rcvd = self.buffer[self.oldest]
      if rcvd then
        self.buffer[self.oldest] = nil
        self.oldest = self.oldest + 1
        return rcvd.data, rcvd.src
      end
      
      assert(not self.suspended_coroutine, "IPC receiver already yielded a coroutine")
      self.suspended_coroutine = coroutine.running()
      return coroutine.yield()
    end,
    
    start = function(self)
      assert(not self.receiver_function, "IPC receiver already started")
      self.receiver_function = function(data, src)
        self.newest = self.newest+1
        self.buffer[self.newest] = {data=data, src=src}
        local coro = self.suspended_coroutine
        if coro then
          self.suspended_coroutine = nil
          local rcvd = self.buffer[self.oldest]
          self.buffer[self.oldest] = nil
          self.oldest = self.oldest+1
          assert(rcvd)
          coroutine.resume(coro, rcvd.data, rcvd.src)
        end
      end
      IPC.receive(self.name, self.src, self.receiver_function)
      return self
    end,
    
    stop = function(self)
      assert(self.receiver_function, "IPC receiver not running")
      IPC.cancel_receive(self.name, self.src, self.receiver_function)
      self.receiver_function = nil
      return self
    end
  },
  __gc = function(self)
    if self.receiver_function then
      self:stop()
    end
  end,
}

function IPC.cancel_receive(name, src_to_cancel, receiver_to_cancel)
  --flexargs. ugly but useful
  local srctype = type(src_to_cancel)
  if srctype == "function" or srctype == "thread" then
    src_to_cancel, receiver_to_cancel = nil, src_to_cancel
  end
  src_to_cancel = validate_process_direction(src_to_cancel, "receive")
  assert(type(name)=="string", "invalid argument #1 to IPC.cancel_receive")
  local num_canceled = 0
  local named_receivers = receivers[name]
  for receiver, src in pairs(named_receivers) do
    if (not src_to_cancel or src_to_cancel == "any" or src_to_cancel == src) and
       (not receiver_to_cancel or receiver == receiver_to_cancel) then
      num_canceled = num_canceled + 1
      if type(receiver) == "thread" then
        coroutine.resume(receiver, nil, "canceled")
      else
        Core.pcall(receiver, nil, "canceled")
      end
    end
  end
  return num_canceled
end

function IPC.receive_from_shuttlesock_core(name, src, data)
  --print("received %s from %s data %s", name, tostring(src), tostring(data))
  local all_ok = true
  local ok, err
  local receivers_for_name = receivers[name]
  for receiver, srcmatch in pairs(receivers_for_name) do
    if srcmatch == "any" or srcmatch == src then
      if type(receiver) == "thread" then
        receivers_for_name[receiver]=nil
        ok, err = coroutine.resume(receiver, data, src)
        if not ok then
          Shuso.set_error(debug.traceback(receiver, ("error receiving IPC message %s from %s: %s"):format(name or "(?)", tostring(src) or "(?)", err)))
          all_ok = false
        end
      else
        assert(type(receiver) == "function")
        ok, err = Core.pcall(receiver, data, src)
        if not ok then --it's already logged
          all_ok = false
        end
      end
    end
  end
  
  return all_ok
end

function IPC.set_ipc_codes(send, reply)
  local registry = debug.getregistry()
  
  local regs = {
    ["shuttlesock.lua_ipc.code"] = send,
    ["shuttlesock.lua_ipc.response_code"] = reply
  }
  for key, val in pairs(regs) do
    assert(registry[key] == nil, ("IPC code %s is already set to %s"):format(key, registry[key]))
    registry[key] = val
  end
  return true
end

setmetatable(IPC, {
  __gxcopy_save_module_state = function()
    local registry = debug.getregistry()
    return {
      code = registry["shuttlesock.lua_ipc.code"],
      reply_code = registry["shuttlesock.lua_ipc.response_code"]
    }
  end,
  __gxcopy_load_module_state = function(data)
    local registry = debug.getregistry()
    registry["shuttlesock.lua_ipc.code"] = data.code
    registry["shuttlesock.lua_ipc.response_code"] = data.reply_code
  end
})

return IPC
