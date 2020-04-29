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
  local ok, err = self.core:connect()
  return io_check_op_completion(self, ok, err)
end

function io:accept()
  local ok, err = self.core:accept()
  return io_check_op_completion(self, ok, err)
end

function io:close()
  local ok, err = self.core:close()
  return io_check_op_completion(self, ok, err)
end

function io:shutdown(how)
  if how == "SHUT_RD" or how == "read" or how == "r" then
    how = "r"
  elseif how == "SHUT_WR" or how == "write" or how == "w" then
    how = "w"
  elseif how == "SHUT_RDWR" or how == "readwrite" or how == "rw" or how == "wr" then
    how = "rw"
  elseif how == nil then
    how = "rw"
  else
    error(("invalid 'how' value %s"):format(tostring(how)))
  end
  local ok, err = self.core:shutdown(how)
  return io_check_op_completion(self, ok, err)
end

function io:write(str, n)
  assert(type(str) == "string", "to write a string")
  if n ~= nil then
    assert(math.type(n) == "integer", "expected string length to be an integer")
  else
    n = #str
  end
  local bytes_written, err, errno = self.core:write(str, n)
  return io_check_op_completion(self, bytes_written, err, errno)
end
function io:write_partial(str, n)
  self.core:op_set_partial(true)
  return self:write(str, n)
end

function io:read(n)
  assert(math.type(n) == "integer", "expected number of bytes to read to be an integer")
  local string_read, err, errno = self.core:read(n)
  return io_check_op_completion(self, string_read, err, errno)
end
function io:read_partial(n)
  self.core:op_set_partial(true)
  return self:read(n)
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
  
  local ok, err, errno = self.core:wait(rw)
  return io_check_op_completion(self, ok, err, errno)
end

function io:closed()
  return self.core:get_closed()
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
    socktype = "SOCK_STREAM"
    sockopts = host.sockopts
    readwrite = host.readwrite or host.rw or "rw"
  elseif type(init) == "table" then
    fd = init.fd
    hostname = init.hostname
    family = init.family
    path = init.path
    name = init.name
    port = init.port
    address = init.address
    address_binary = init.address_binary
    socktype = init.type or init.socktype
    sockopts = init.sockopts
    readwrite = init.readwrite or init.rw or "rw"
    
    if not name then
      if path then
        name = "unix:"..path
      elseif address and port then
        name = address .. ":"..port
      elseif address then
        name = address
      end
    end
  else
    error("invalid IP init type " .. type(init))
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
    elseif socktype then
      error("invalid socket type '" .. tostring(socktype) .. "'")
    else --default to TCP
      socktype = "SOCK_STREAM"
    end
    
    self.init = {
      address = address,
      address_binary = address_binary,
      hostname = hostname,
      family = family,
      path = path,
      name = name,
      port = port,
      type = socktype,
      sockopts = sockopts,
      readwrite = readwrite
    }
    
    if not fd or fd == -1 then
      local err
      fd, err = Core.fd_create(family, socktype, sockopts)
      if not fd then
        error(err)
      end
      
      self.init.fd = fd
    else
      self.init.fd = fd
    end
    
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
