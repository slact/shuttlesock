local Core = require "shuttlesock.core"
--local Shuso = require "shuttlesock"
local Process = require "shuttlesock.process"
local Log = require "shuttlesock.log"
local coroutine = require "shuttlesock.coroutine"
local IPC = {}
local debug = require "debug"

IPC.code = 0
IPC.reply_code = 0

local all_waiting_handlers_refcounts = {}

local function waiting_handler_refcount_increment(handler)
  local cur = rawget(all_waiting_handlers_refcounts, handler)
  if cur == nil then
    rawset(all_waiting_handlers_refcounts, handler, 1)
  else
    assert(type(cur)=="number")
    rawset(all_waiting_handlers_refcounts, handler, cur + 1)
  end
  return true
end
local function waiting_handler_refcount_decrement(handler)
  local cur = rawget(all_waiting_handlers_refcounts, handler)
  if cur == nil then
    error("expected handler " .. tostring(handler).. " to have a waiting_handler reference, but it's not there")
  elseif cur == 1 then
    rawset(all_waiting_handlers_refcounts, handler, nil)
  else
    assert(type(cur) == "number")
    assert(cur > 1)
    rawset(all_waiting_handlers_refcounts, handler, cur - 1)
  end
  return true
end

local function ipc_coroutine_yield(kind)
  local coro = coroutine.running()
  waiting_handler_refcount_increment(coro, "handler")
  local data, src = coroutine.yield()
  waiting_handler_refcount_decrement(coro)
  return data, src
end

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

local fd_receivers = {}

local function validate_process_direction(process, direction, no_multi)
  local ptype = type(process)
  local many = false
  if ptype == "number" then
    if not Core.procnum_valid(process) then
      error("invalid "..(direction or "").." procnum " .. (process or ""))
    end
  elseif not process then
    if direction == "receive" then
      process = "any"
    else
      error("missing "..(direction or "").." procnum " .. (process or ""))
    end
  elseif process == "any" and direction == "receive" then
    process = "any"
    many = true
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
    error("invalid "..(direction or "").." procnum " .. (process or ""))
  end
  if no_multi and many then
    error("invalid "..(direction or "").." procnum " .. (process or ""))
  end
  return process, many
end

local function run_handler(for_what, name, handler, ...)
  local ok, err
  local handler_type = type(handler)
  if handler_type == "function" then
    ok, err = xpcall(handler, debug.traceback, ...)
  else
    assert(handler_type == "thread")
    ok, err = coroutine.resume(handler, ...)
  end
  if not ok then
    Log.error("Error while running Lua IPC message '%s' %s %s: %s", name, for_what, handler_type, err)
    return false
  end
  return true
end

local function ipc_broadcast(dst, name, data, handler, must_yield)
  local my_procnum = dst == "others" or dst == "other_workers" and Process.procnum()
  local min_procnum = dst == ("workers" or dst == "other_workers") and 0 or -math.huge
  
  if Process.runstate() ~= "running" then
    return nil, ("%s state must be 'running' but is '%s'"):format(Process.procnum_to_string(Process.procnum()), Process.runstate())
  end
  
  local dstprocnums = {}
  for _, procnum in ipairs(Process.all_procnums()) do
    if procnum ~= my_procnum and procnum >= min_procnum then
      local procstate = Process.runstate(procnum)
      if procstate ~= "running" then
        return nil, ("%s state must be 'running' but is '%s'"):format(Process.procnum_to_string(procnum), procstate)
      end
      table.insert(dstprocnums, procnum)
    end
  end
  local need_shmem = not Process.share_heap(Process.procnum(), dstprocnums)
  
  local packed_data = assert(Core.ipc_pack_message_data(data, name, need_shmem))
  
  local responses_pending, failed_count = #dstprocnums, 0
  local ipc_send_acknowledged_handler_coro
  ipc_send_acknowledged_handler_coro = coroutine.create(function(success)
    waiting_handler_refcount_increment(ipc_send_acknowledged_handler_coro, "breadcast_handler")
    while true do
      responses_pending = responses_pending - 1
      if not success then
        failed_count = failed_count + 1
      end
      if responses_pending == 0 then
        break
      end
      success = ipc_coroutine_yield("broadcast")
    end
    assert(Core.ipc_gc_message_data(packed_data))
    local ret = true
    if handler then
       ret = run_handler("broadcast", name, handler, #dstprocnums)
    end
    waiting_handler_refcount_decrement(ipc_send_acknowledged_handler_coro)
    return ret
  end)
  for _, procnum in ipairs(dstprocnums) do
    local ok = Core.ipc_send_message(procnum, nil, nil, packed_data, ipc_send_acknowledged_handler_coro)
    assert(ok)
  end
  
  if must_yield then
    assert(handler == coroutine.running())
    return ipc_coroutine_yield("broadcast")
  else
    return #dstprocnums
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
    local src_state = Process.runstate()
    local dst_state = Process.runstate(dst)
    if src_state ~= "running" then
      return nil, ("%s state must be 'running' but is '%s'"):format(Process.procnum_to_string(Process.procnum()), src_state)
    elseif dst_state ~= "running" then
      return nil, ("%s state must be 'running' but is '%s'"):format(Process.procnum_to_string(dst), dst_state)
    end
    
    local ok = Core.ipc_send_message(dst, name, data, nil, handler)
    if must_yield then
      return ipc_coroutine_yield("send")
    else
      return ok and 1
    end
  end
end

function IPC.send_fd(dst, ref, fd)
  dst = validate_process_direction(dst, "send", true)
  
  assert(type(ref)=="number", "FD receiver ref must be a number")
  assert(type(fd)=="number", "FD must be a number")
  
  return Core.ipc_send_fd(dst, fd, ref)
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
  src = validate_process_direction(src, "receive")
  if type(receiver) == "function" then
    assert(type(receiver) == "function", "expected receiver argument to be a function")
    receivers[name][receiver]=src
    return true
  else
    if not receiver then
      assert(coroutine.isyieldable(), "can't receive IPC message from outisde a yieldable coroutine")
      receiver = coroutine.running()
    end
    assert(type(receiver) == "thread")
    receivers[name][receiver]=src
    return ipc_coroutine_yield("receive")
  end
end


local all_active_receivers = setmetatable({}, {__mode="k"})
local receiver_mt
local function new_receiver(name, opt)
  local src = opt and opt.src
  local timeout = opt and opt.timeout
  local self = {buffer = {}, oldest=1, newest=0, name = name, src = src, timeout = timeout}
  setmetatable(self, receiver_mt)
  return self
end

IPC.Receiver = {}


function IPC.Receiver.new(name, src)
  local receiver = new_receiver(name, {src=src})
  function receiver:receive_start()
    IPC.receive(self.name, self.src, self.receiver_function)
  end
  function receiver:receive_stop()
    IPC.cancel_receive(self.name, self.src, self.receiver_function)
  end
  return receiver
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
        return table.unpack(rcvd, 1, rcvd.n)
      end
      
      assert(not self.suspended_coroutine, "IPC receiver already yielded a coroutine")
      self.suspended_coroutine = coroutine.running()
      local data, src = ipc_coroutine_yield("buffered receiver")
      self.suspended_coroutine = nil
      return data, src
    end,
    
    start = function(self)
      assert(not self.receiver_function, "IPC receiver already started")
      self.receiver_function = function(...)
        self.newest = self.newest+1
        local incoming = table.pack(...)
        self.buffer[self.newest] = incoming
        local coro = self.suspended_coroutine
        if coro then
          local rcvd = self.buffer[self.oldest]
          self.buffer[self.oldest] = nil
          self.oldest = self.oldest+1
          assert(rcvd)
          assert(type(coro) == "thread")
          run_handler("buffered receiver", self.name, coro, table.unpack(rcvd, 1, rcvd.n))
        end
      end
      self:receive_start()
      all_active_receivers[self]=true
      return self
    end,
    
    stop = function(self)
      assert(self.receiver_function, "IPC receiver not running")
      all_active_receivers[self]=nil
      self:receive_stop()
      self.receiver_function = nil
      assert(self.suspended_coroutine == nil)
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
      run_handler("cancel-receive", name, receiver, nil, "canceled")
    end
  end
  return num_canceled
end

function IPC.receive_from_shuttlesock_core(name, src, data)
  local all_ok = true
  local ok
  local receivers_for_name = receivers[name]
  for receiver, srcmatch in pairs(receivers_for_name) do
    if srcmatch == "any" or srcmatch == src then
      ok = run_handler("receive", name, receiver, data, src)
      all_ok = all_ok and ok
    end
  end
  
  return all_ok
end

function IPC.receive_fd_from_shuttlesock_core(ok, ref, fd)
  local receiver = fd_receivers[ref]
  if not receiver then
    require "shuttlesock.log".error(("No Lua fd receiver for ref %d fd %d"):format(ref, fd))
    return
  end
  receiver(ok, fd)
end

IPC.FD_Receiver = {}

function IPC.FD_Receiver.new(name, timeout)
  local receiver = new_receiver(name, {timeout=timeout})
  function receiver:receive_start()
    self.ref = Core.ipc_receive_fd_start(self.name or "", self.timeout)
    self.id = self.ref
    assert(self.receiver_function)
    fd_receivers[self.ref]=self.receiver_function
  end
  function receiver:receive_stop()
    fd_receivers[self.ref]=nil
    Core.ipc_receive_fd_stop(self.ref)
    self.ref = nil
  end
  return receiver
end

function IPC.FD_Receiver.start(name, timeout)
  return IPC.FD_Receiver.new(name, timeout):start()
end

function IPC.shutdown_from_shuttlesock_core()
  local receivers_copy = {}
  for k, v in pairs(all_active_receivers) do
    receivers_copy[k]=v
  end
  for receiver in pairs(receivers_copy) do
    receiver:stop()
  end
end


IPC.debug = {}
local function table_copy(tbl)
  local cpy = {}
  for k, v in pairs(tbl) do
    cpy[k]=v
  end
  return cpy
end
function IPC.debug.waiting_handlers()
  return table_copy(all_waiting_handlers_refcounts)
end
function IPC.debug.active_receivers()
  return table_copy(all_active_receivers)
end
function IPC.debug.receivers()
  return table_copy(receivers)
end
function IPC.debug.fd_receivers()
  return table_copy(fd_receivers)
end
  


return IPC
