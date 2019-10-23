local Core = require "shuttlesock.core"
local Process = require "shuttlesock.process"

local IPC = {}

--[[ receivers are not copied from manager to workers, and must be re-initialized
at the start of each worker. this is to permit the use of coroutine-based
receivers, which cannot be gxcopied between independent Lua states
]]
local receivers = setmetatable({}, {
  __newindex = function(t, k)
    local tt = {}
    rawset(t, k, tt)
    return tt
  end
})

local yieldable = coroutine.yieldable
local send_message_yield = Core.send_message_yield

function IPC.send(destination, type, data)
  assert(yieldable(), "can't send IPC message from outisde a yieldable coroutine")
  local code = handlers
  
  return send_message_yield(destination, code, data)
end

function IPC.receive(name, src, receiver_function)
  local src_type = type(src)
  if src_type == "number" then
    assert(Core.Lua_shuso_procnum_valid(source_procnum), "invalid source procnum")
  elseif not src then
    src = "any"
  elseif src == "master" then
    src = -2
  elseif src == "manager" then 
    src = -1
  else
    error("invalid source procnum")
  end
  
  if handlers[name] then
    error("IPC message type \"%s\" is already registered")
  end
  if timeout then
    error("Timeouts for IPC receivers not yet implemented")
  end
  
  local receiver
  if receiver_function then
    receiver = receiver_function
    assert(type(receiver) == "function", "expected receiver argument to be a function")
    receivers[name][receiver]=src
    return true
  else
    receiver = coroutine.running()
    assert(yieldable(), "can't receive IPC message from outside a yieldable coroutine")
    receivers[name][receiver]=src
    return coroutine.yield()
  end
end

function IPC.receive_from_shuttlesock_core(name, src, data)
  local all_ok = true
  local ok, err
  for receiver, srcmatch in pairs(receivers[name]) do
    if srcmatch == "any" or srcmatch == src then
      if type(receiver) == "thread" then
      ok, err = coroutine.resume(data, src)
      if not ok then
        Shuso.set_error(debug.traceback(receiver, "error receiving IPC message: "..err))
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
  
  return all_ok
end

setmetatable(IPC, {
  __gxcopy_save_module_state = function()
    --nothing to copy, but maybe in the future?...
    return { }
  end,
  __gxcopy_load_module_state = function(data)
    --nothing to load for now, but maybe later?
  end
})

return IPC
