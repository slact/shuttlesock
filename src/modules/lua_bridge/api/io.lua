local Core = require "shuttlesock.core"
--local Log = require "shuttlesock.log"
local shuso_coroutine = require "shuttlesock.coroutine"
local IO = {}

local io = {}

local function io_check_op_completion(self, ...)
  print("ready?")
  if not self.core:op_completed() then
    print("yieldplease")
    return coroutine.yield()
  else
  print("it's ready")
    return ...
  end
end

function io:connect()
  print("HIO")
  local ok, err = self.core:op("connect")
  print("HIO2")
  return io_check_op_completion(self, ok, err)
end

local function io_write(self, str, n, partial)
  assert(type(str) == "string")
  if n then
    assert(type(n) == "number")
  else
    n = #str
  end
  local bytes_written, err = self.core:op(partial and "write_partial" or "write", str, n)
  return io_check_op_completion(self, bytes_written, err)
end

function io:write(str, n)
  return io_write(str, n, false)
end

function io:write_partial(str, n)
  return io_write(str, n, true)
end

local function io_read(self, n, partial)
  assert(type(n) == "string")
  local bytes_written, err = self.core:op(partial and "read_partial" or "read", n)
  return io_check_op_completion(self, bytes_written, err)
end

function io:read(n)
  return io_read(self, n, false)
end
function io:read(n)
  return io_read(self, n, true)
end

local io_mt = {
  __index = io
}

function IO.wrap(init, io_handler)
  local fd, hostname, family, path, name, port, address, address_binary, socktype, sockopts, readwrite
  
  if type(init) == "integer" then
    fd = init
    name = "fd:"..fd
    readwrite = "rw"
  elseif type(init) == "string" then
    local host, err = Core.parse_host(init)
    require"mm"(host)
    if not host then
      return nil, err
    end
    hostname = host.hostname
    family = host.family
    path = host.path
    name = host.name
    port = host.port
    sockopts = host.sockopts
    readwrite = host.readwrite or host.rw or "rw"
  else
    error("not yet implemented")
  end
  
  local self = setmetatable({}, io_mt)
  
  if not io_handler and coroutine.isyieldable() then
    local coro = coroutine.running()
    self.parent_handler_coroutine = coro
    io_handler = function(...)
      local ok, err = coroutine.resume(coro)
      if not ok then
        error(debug.traceback(coro, err))
      end
    end
  end
  assert(type(io_handler) == "function", "io_handler coroutine function missing, and not running from yieldable coroutine")
  
  self.coroutine = coroutine.create(function()
    if hostname then
      local resolved, err = Core.resolve(hostname)
      if not resolved then
        error(("Failed to start IO coroutine: couldn't resolve hostname '%s': %s"):format(hostname, err or ""))
      end
      local addr = resolved.address
      address = addr.address
      family = addr.family
      address_binary = addr.address_binary
    end
    if family == "IPv4" or family == "ipv4" or family == "AF_INET" then
      family = "AF_INET"
    elseif family == "IPv6" or family == "ipv6" or family == "AF_INET6" then
      family = "AF_INET6"
    elseif family == "Unix" or family == "unix" or family == "AF_UNIX" then
      family = "AF_UNIX"
    elseif family == "local" or family == "AF_LOCAL" then
      family = "AF_LOCAL"
    else
      error("failed to start IO coroutine: invalid address family " .. family)
    end
    
    if socktype == "TCP" or socktype == "tcp" or socktype == "stream"  or socktype == "SOCK_STREAM" then
      socktype = "SOCK_STREAM"
    elseif socktype == "UDP" or socktype == "udp" or socktype == "dgram" or socktype == "datagram" or socktype == "SOCK_DGRAM" then
      socktype = "SOCK_DGRAM"
    elseif socktype == "raw" or socktype == "SOCK_RAW" then
      socktype = "SOCK_RAW"
    elseif path then
      socktype = "SOCK_STREAM"
    else --default to TCP
      socktype = "SOCK_STREAM"
    end
    
    self.init = {
      addr = address,
      addr_binary = address_binary,
      hostname = hostname,
      addr_family = family,
      path = path,
      name = name,
      port = port,
      socktype = socktype,
      sockopts = sockopts,
      readwrite = readwrite
    }
    
    local err
    fd, err = Core.fd_create(family, socktype, sockopts)
    if not fd then
      error(err)
    end
    
    self.init.fd = fd
    
    self.core = assert(Core.io_create(self.init, self.coroutine))
  end)
  
  return function(...)
    shuso_coroutine.resume(self.coroutine, self, ...)
    if self.parent_handler_coroutine then
      assert(coroutine.status(self.parent_handler_coroutine) == "running")
      return self, ...
    else
      return true
    end
  end
end

return IO
