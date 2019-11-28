local Core = require "shuttlesock.core"
local Shuso = require "shuttlesock"
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

local function validate_process(process, direction)
  local ptype = type(process)
  if ptype == "number" then
    assert(Core.procnum_valid(process), "invalid "..(direction or "").." procnum")
  elseif not process then
    if direction == "receive" then
      process = "any"
    else
      error("missing "..(direction or "").." procnum")
    end
  elseif process == "master" then
    process = -2
  elseif process == "manager" then
    process = -1
  else
    error("invalid "..(direction or "").." procnum")
  end
  return process
end

function IPC.send(destination, name, data)
  assert(coroutine.isyieldable(), "can't send IPC message from outisde a yieldable coroutine")
  local dst = validate_process(destination, "send")
  assert(dst ~= "any", "invalid send procnum")
  assert(type(name)=="string", "invalid argument #2 to IPC.send")
  assert(data ~= nil, "invalid argument #3 to IPC.send")
  return Core.ipc_send_message(dst, name, data)
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

function IPC.cancel_receive(name, src_to_cancel, receiver_to_cancel)
  --flexargs. ugly but useful
  local srctype = type(src_to_cancel)
  if srctype == "function" or srctype == "thread" then
    src_to_cancel, receiver_to_cancel = nil, src_to_cancel
  end
  src_to_cancel = validate_process(src_to_cancel, "receive")
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
  --Log.info("received %s from %s data %s", name, tostring(src), tostring(data))
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
        ok = Core.pcall(receiver, data, src)
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
