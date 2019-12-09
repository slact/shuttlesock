local Module = require "shuttlesock.module"
local Utils = require "shuttlesock.utils"
local Semaphore = {}
--local Semaphore = require "shuttlesock.semaphore"

-- luacheck: ignore CFuncs
local CFuncs = require "shuttlesock.modules.core.server.cfuncs"

local Server = Module.new {
  name = "server",
  version = require "shuttlesock".VERSION,
  publish = {
    listen_token= {data_type="string"}
  }
}

Server.settings = {
  {
    name = "http",
    path = "/",
    description = "Configuration block for http and http-like protocol servers",
    nargs = 0,
    block = true
  },
  {
    name = "stream",
    path = "/",
    description = "Configuration block for TCP or UDP servers",
    nargs = 0,
    block = true
  },
  {
    name = "server",
    path = "(http|stream)/",
    description = "Configuration block for a server.",
    nargs = 0,
    block = true
  },
  {
    name = "listen",
    path = "server/",
    description = "Sets the address and port for the socket on which the server will accept connections. It is possible to specify just the port. The address can also be a hostname.",
    default_value = "$default_listen_host:$default_listen_port",
    nargs = "1-32",
  }
}

function Server:initialize_config(block)
  local ctx = block:context(self)
  local listen = block:setting("listen")
  if not listen then
    block:error('"listen" setting issing in "server" block')
  end
  
  local str = listen:value(1, "string")
  local parsed_host, err = Utils.parseHost(str)
  if not parsed_host then
    listen:error(err)
  end
  
  ctx.listen = parsed_host
  
  if not parsed_host.port then
    if block:match_path("http/") then
      parsed_host.port = Utils.master_has_superuser() and 80 or 8000
    else
      listen:error("no port specified for stream server")
    end
  end
  
  self.server_listen = self.server_listen or {hosts = {}, by_binding = {}}
  
  self.server_listen[parsed_host.port] = self.server_listen[parsed_host.port] or {}
  
  self.hosts_resolved = Semaphore.new()
  
  table.insert(self.server_listen.hosts, parsed_host)
end

Server:subscribe("master.start", function()
  --TODO
end)

Server:subscribe("worker.start", function()
  coroutine.wrap(function()
    --TODO
  end)
end)

return Server
