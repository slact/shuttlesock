local Module = require "shuttlesock.module"
local Core = require "shuttlesock.core"
local coroutine = require "shuttlesock.coroutine"
local Process = require "shuttlesock.process"
local IPC = require "shuttlesock.ipc"
local Log = require "shuttlesock.log"

-- luacheck: ignore CFuncs
local CFuncs = require "shuttlesock.modules.core.server.cfuncs"

local Server = Module.new {
  name = "server",
  version = require "shuttlesock".VERSION,
  publish = {
    ["accept"] = {
      data_type = "server_accept"
    },
    ["http.accept"] = {
      data_type = "server_accept"
    },
    ["stream.accept"] = {
      data_type = "server_accept"
    }
  },
  raw_hosts = {},
  bindings = {}
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

local function deduplicated_getaddrinfo(addrinfo)
  local addrs_unique = {}
  for _, addr in pairs(addrinfo) do
    addrs_unique[(addr.family or "?")..":"..(addr.address or "?")] = addr
    addr.socktype = nil
  end
  
  local addrs = {}
  for _, v in pairs(addrs_unique) do
    table.insert(addrs, v)
  end
  
  return addrs
end

local function parse_host(str)
  local ipv4, ipv6, hostname, port
  local addrinfo, path, err
  if str:match("^unix:.+") then -- unix socket
    path = str:match("^unix:(.+)")
  elseif str:match("^%[.*%]") then --ipv6
    ipv6, port = str:match("^[(.*)]$"), false
    if not ipv6 then
      ipv6, port = str:match("^[(.*)]:(%d+)$")
    end
    if not ipv6 then
      return "invalid IPv6 format"
    end
    addrinfo, err = CFuncs.getaddrinfo_noresolve(ipv6, 6)
    
  elseif str:match("^%d+%.%d+%.%d+%.%d+") then --ipv4
    ipv4, port = str:match("^([%d%.]+)$"), false
    if not ipv4 then
      ipv4, port = str:match("^([%d%.]+):(%d+)$")
    end
    if not ipv4 then
      return nil, "invalid IPv4 address"
    end
    addrinfo, err = CFuncs.getaddrinfo_noresolve(ipv4, 4)
    
  else -- hostname maybe?
    hostname, port = str:match("^([%l%u%d%.%-%_]+)$"), false
    if not hostname then
      hostname, port = str:match("^([%l%u%d%.%-%_]+):(%d+)$")
    end
    if not hostname or hostname:match("%.%.") or hostname:match("%-$") then
      return nil, "invalid hostname"
    end
    addrinfo = nil
  end
  
  if err then
    return nil, err
  end
  
  if addrinfo then
    addrinfo = deduplicated_getaddrinfo(addrinfo)
  end
  
  if port then
    port = tonumber(port)
    if port == nil or (port and (port < 0 or port >= 2^16)) then
      return nil, "invalid port"
    end
  end
  
  if not port then port = nil end
  
  return {
    name = (addrinfo and addrinfo.address) or str,
    port = port,
    addrinfo = addrinfo,
    hostname = hostname,
    path = path
  }
end

function Server:initialize_config(block)
  if not block:match_path("/(http|stream)/server/") then
    return
  end
  local listen = block:setting("listen")
  if not listen then
    return block:error('"listen" setting missing')
  end
  
  local str = listen:value(1, "string")
  local host, err = parse_host(str)
  if not host then
    return listen:error(err)
  end

  host.block = block
  host.type = block:parent_block().name
  host.setting = listen
  table.insert(self.raw_hosts, host)
end

local function common_parent_block(blocks)
  local parents = {}
  local maxlen = 0
  for _, block in pairs(blocks) do
    local cur = block
    local pchain = {}
    repeat
      table.insert(pchain, 1, cur)
      cur = cur:parent_block()
    until not cur
    
    table.insert(parents, pchain)
    if maxlen < #pchain then
      maxlen = #pchain
    end
  end
  local cur, common, last_common
  for i=0, maxlen do
    cur, common = nil, nil
    for _, pchain in pairs(parents) do
      cur = pchain[i]
      if common == nil then
        common = cur
      elseif cur == nil or cur ~= common then
        return last_common
      end
    end
    last_common = cur
  end
end

Server:subscribe("core:manager.workers_started", function()

  local coro = coroutine.create(function()
    for _, host in ipairs(Server.raw_hosts) do
      local addrs, err
      if host.addrinfo then
        addrs = host.addrinfo
      elseif host.hostname then
        --TODO: per-block resolver
        local resolved
        resolved, err = Core.resolve(host.hostname)
        if resolved then
          addrs = resolved.addresses
          --host.aliases = resolved.aliases
          --host.name = resolved.name
        end
      elseif host.path then
        addrs = {
          {
            family = "unix",
            path=host.path
          }
        }
      end
      host.addresses = addrs
      if not addrs or err then
        return nil, host.setting:error(err or "failed to process host")
      end
    end
    local unique_bindings = {}
    local unique_binding_blocks = {}
    for _, host in ipairs(Server.raw_hosts) do
      for _, addr in ipairs(host.addresses) do
        addr.port = host.port
        local id
        if addr.address then
          id = addr.address ..":"..addr.port or "default"
        elseif addr.path then
          id = addr.path
        else
          return nil, host.setting:error("can't figure out internal id")
        end
        unique_bindings[id]=unique_bindings[id] or {address = addr, listen = {}}
        table.insert(unique_bindings[id].listen, {block = host.block, setting = host.setting, type=host.type})
        unique_binding_blocks[id] = unique_binding_blocks[id] or {}
        table.insert(unique_binding_blocks[id], host.block)
      end
    end
    
    Server.bindings = {}
    for id, binding in pairs(unique_bindings) do
      binding.name = id
      binding.common_parent_block = common_parent_block(unique_binding_blocks[id])
      table.insert(Server.bindings, binding)
      
      local host_type
      for _, host in pairs(binding.listen) do
        if not host_type then
          host_type = host.type
        elseif host_type ~= binding.type then
          return nil, host.setting:error("can't listen on the same address as server type '" .. host_type.."'")
        end
      end
      binding.type = host_type
    end
    
    --require"mm"(Server.bindings)
    
    --for _, binding in pairs(Server.bindings) do
    --
    --end
    
    local worker_procnums = Process.worker_procnums()
    
    if #Server.bindings > 0 then
      
      local rcv = IPC.Receiver.start("server:create_listener_sockets", "master")
      local rcvfd = IPC.FD_Receiver.start("server:receive_listener_sockets", 5.0)
      
      for i, binding in ipairs(Server.bindings) do

        binding.ptr = assert(CFuncs.create_binding_data(binding, i), "problem creating bind data")
        local shared_ptr = CFuncs.create_shared_host_data(binding.ptr)
        
        local msg = {
          count = #worker_procnums,
          shared_ptr = shared_ptr,
          fd_ref = rcvfd.id
        }
        
        IPC.send("master", "server:create_listener_sockets", msg)
        local resp = rcv:yield()
        binding.sockets = binding.sockets or {}
        for _, err in ipairs(resp.errors or {}) do
          Log.error(err)
        end
        while #binding.sockets < resp.fd_count do
          local ok, fd = rcvfd:yield()
          assert(ok)
          table.insert(binding.sockets, fd)
        end
        IPC.send("master", "server:create_listener_sockets", "ok")
        
        CFuncs.free_shared_host_data(shared_ptr)
      end
    end
    for _, worker in ipairs(worker_procnums) do
      if #Server.bindings > 0 then
        local receiver = IPC.Receiver.start("server:listener_socket_transfer", worker)
        for _, binding in ipairs(Server.bindings) do
          local fd = assert(table.remove(binding.sockets), "not enough listener sockets opened. weird")
          --print("uuuh send " .. binding.name .. " fd: " .. fd .. " to worker " .. worker)
          IPC.send(worker, "server:listener_socket_transfer", {name = binding.name, fd = fd, address = binding.address, binding_ptr = binding.ptr})
          local resp = receiver:yield()
          assert(resp == "ok", resp)
        end
        receiver:stop()
      end
      IPC.send(worker, "server:listener_socket_transfer", "done")
    end
    IPC.send("master", "server:create_listener_sockets", "done")
    Server.startup_finished = true
  end)
  coroutine.resume(coro)
end)

Server:subscribe("core:worker.start", function()
  Server.listener_io_c_coroutines = {}
  local coro = coroutine.create(function()
    local receiver = IPC.Receiver.start("server:listener_socket_transfer", "manager")
    while true do
      local data = receiver:yield()
      if data == "done" then
        break
      elseif type(data) == "table" then
        local c_io_coro = CFuncs.start_worker_io_listener_coro(Server, data.fd, data.ptr, data.binding_ptr)
        local resp
        if c_io_coro then
          table.insert(Server.listener_io_c_coroutines, c_io_coro)
          resp = "ok"
        else
          resp = "not ok"
        end
        IPC.send("manager", "server:listener_socket_transfer", resp)
      else
        IPC.send("manager", "server:listener_socket_transfer", "i don't get it")
      end
    end
    receiver:stop()
    Server.startup_finished = true
  end)
  
  coroutine.resume(coro)
end)

Server:subscribe("core:master.start", function()
  local coro = coroutine.create(function()
    local rcv = IPC.Receiver.start("server:create_listener_sockets", "manager")
    repeat
      local req, src = rcv:yield()
      if type(req) == "table" then
        local fds = {}
        local errors = {}
        while #fds < req.count - #errors do
          local fd, err = CFuncs.handle_fd_request(req.shared_ptr)
          if fd then
            table.insert(fds, fd)
          else
            table.insert(errors, err or "error")
          end
        end
        
        local resp = {
          fd_count = #fds,
          errors = errors
        }
        IPC.send(src, "server:create_listener_sockets", resp)
        for _, fd in ipairs(fds) do
          IPC.send_fd(src, req.fd_ref, fd)
        end
        resp = rcv:yield()
        assert(resp == "ok")
      end
    until not req or req == "done"
    rcv:stop()
    Server.startup_finished = true
  end)
  coroutine.resume(coro)
end)

local function delay_stopping(self, evstate)
  if not Server.startup_finished then
    assert(evstate:delay("still initializing", 0.3))
  end
end

Server:subscribe("core:master.stop", delay_stopping)
Server:subscribe("core:manager.stop", delay_stopping)
Server:subscribe("core:worker.stop", delay_stopping)

Server:add()

return Server
