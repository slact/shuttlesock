local Core = require "shuttlesock.core"
--local Log = require "shuttlesock.log"
local coroutine = require "shuttlesock.coroutine"

local IO = {}

local io = {}
function io:start()
  assert(type(self.coroutine) == "thread")
  coroutine.resume(self.coroutine)
end

local io_mt = {
  __index = io
}

function IO.new(init, io_handler)
  assert(type(io_handler) == "function", "IO.new last parameter must be a function for the IO coroutine")
  
  local fd, hostname, family, path, name, port, address, address_binary, socktype, sockopts
  
  if type(init) == "integer" then
    fd = init
    name = "fd:"..fd
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
  else
    error("not yet implemented")
  end
  
  local self = setmetatable({}, io_mt)
  
  local run_handler = function()
    --TODO
  end
  
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
    if family == "IPv4" or family == "ipv4" then
      family = "AF_INET"
    elseif family == "IPv6" or family == "ipv6" then
      family = "AF_INET6"
    elseif family == "Unix" or family == "unix" then
      family = "AF_UNIX"
    elseif family == "local" then
      family = "AF_LOCAL"
    else
      error("failed to start IO coroutine: invalid address family " .. family)
    end
    
    if socktype == "TCP" or socktype == "tcp" or socktype == "stream" then
      socktype = "SOCK_STREAM"
    elseif socktype == "UDP" or socktype == "udp" or socktype == "dgram" or socktype == "datagram" then
      socktype = "SOCK_DGRAM"
    elseif socktype == "raw" then
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
      sockopts = sockopts
    }
    
    local err
    fd, err = Core.fd_create(family, socktype, sockopts)
    if not fd then
      error(err)
    end
    
    self.init.fd = fd
    
    self.io = assert(Core.io_create(self.init, run_handler))
    
    require"mm"(self.init)
    
    require"mm"(self.io)
    
    
    
  end)
  
  return self
end

return IO
