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
  
  local fd, hostname, family, path, name, port, address, address_binary
  
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
  else
    error("not yet implemented")
  end
  
  local self = setmetatable({}, io_mt)
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
    self.init = {
      address = address,
      address_binary = address_binary,
      hostname = hostname,
      family = family,
      path = path,
      name = name,
      port = port
    }
    
    assert(self.address_binary or self.path)
  end)
  
  return self
end

return IO
