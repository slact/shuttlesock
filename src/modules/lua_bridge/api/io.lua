local Core = require "shuttlesock.core"
--local Log = require "shuttlesock.log"
local shuso_coroutine = require "shuttlesock.coroutine"
local IO = {}

local io = {}

local function io_check_op_completion(self, ...)
  if not self.core:op_completed() then
    assert(self.coroutine == coroutine.running(), "called io yielding function from a foreign coroutine...")
    return coroutine.yield()
  else
    return ...
  end
end

function io:connect()
  local ok, err = self.core:op("connect")
  return io_check_op_completion(self, ok, err)
end

local function io_write(self, str, n, partial)
  assert(type(str) == "string")
  if n then
    assert(type(n) == "number")
  else
    n = #str
  end
  local op = partial and "write_partial" or "write"
  local bytes_written, err, errno = self.core:op(op, str, n)
  if not bytes_written and errno == "EAGAIN" then
    self:wait("w")
    bytes_written, err = self.core:op(op, str, n)
  end
  
  return io_check_op_completion(self, bytes_written, err)
end

function io:write(str, n)
  return io_write(self, str, n, false)
end

function io:write_partial(str, n)
  return io_write(self, str, n, true)
end

local function io_read(self, n, partial)
  assert(type(n) == "number")
  local op = partial and "read_partial" or "read"
  local string_read, err, errno = self.core:op(op, n)
  if not string_read and errno == "EAGAIN" then
    self:wait("r")
    string_read, err = self.core:op(op, n)
  end
  return io_check_op_completion(self, string_read, err)
end

function io:read(n)
  return io_read(self, n, false)
end
function io:read_partial(n)
  return io_read(self, n, true)
end

function io:wait(rw)
  if rw == "read" or rw == "r" then
    rw = "r"
  elseif rw == "write" or rw == "w" then
    rw = "w"
  elseif rw == "readwrite" or rw == "wr" or rw == "rw" then
    rw = "rw"
  else
    error(("invalid rw value '%s' for io:wait()"):format(tostring(rw)))
  end
  
  local ok, err = self.core:op("wait", rw)
  return io_check_op_completion(self, ok, err)
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
  
  local handler_coroutine
  local handler_is_parent
  
  if type(io_handler) == "function" then
    handler_coroutine = coroutine.create(io_handler)
  elseif type(io_handler) == "thread" then
    handler_coroutine = io_handler
  elseif not io_handler and coroutine.isyieldable() then
    handler_coroutine = coroutine.running()
    handler_is_parent = true
  end
  assert(type(handler_coroutine) == "thread", "io_handler coroutine function missing, and not running from yieldable coroutine")
  self.coroutine = handler_coroutine
  
  local function initialize()
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
    
    return self
  end
  
  return function(...)
    if handler_is_parent then
      initialize()
      return self, ...
    else
      local init_coro = coroutine.create(function(...)
        initialize()
        return shuso_coroutine.resume(self.coroutine, self, ...)
      end)
      shuso_coroutine.resume(init_coro, ...)
      return true
    end
  end
end

return IO
