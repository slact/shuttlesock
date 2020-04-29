local Module = require "shuttlesock.module"
local Core = require "shuttlesock.core"
local coroutine = require "shuttlesock.coroutine"
local Process = require "shuttlesock.process"
local IPC = require "shuttlesock.ipc"
local Log = require "shuttlesock.log"
local Atomics = require "shuttlesock.atomics"

-- luacheck: ignore CFuncs
local CFuncs = require "shuttlesock.modules.core.server.cfuncs"

local Server = Module.new {
  name = "server",
  version = require "shuttlesock".VERSION,
  publish = {
    ["maybe_accept"] = {
      data_type = "server_maybe_accept"
    },
    ["http.accept"] = {
      data_type = "server_accept"
    },
    ["stream.accept"] = {
      data_type = "server_accept"
    },
    "start",
    "master.start",
    "manager.start",
    "worker.start"
  },
  subscribe = {
    "~server:maybe_accept"
  },
  raw_hosts = {},
  bindings = {},
  bindings_by_ptr = {}
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

function Server:initialize()
  CFuncs.register_event_data_types()
  CFuncs.maybe_accept_event_init(self:event_pointer("maybe_accept"))
  self.shared = Atomics.new("done", "failed")
  self.shared.done = 0
  self.shared.failed = 0
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
  local host, err = Core.parse_host(str)
  if not host then
    return listen:error(err)
  end
  
  host.socket_type = "TCP"
  for _, val in listen:each_value(2, "string") do
    if val == "udp" or val == "UDP" then
      host.socket_type = "UDP"
    elseif val == "tcp" or val == "TCP" then
      host.socket_type = "TCP"
    end
  end
  
  host.block = block
  host.server_type = block:parent_block().name
  host.setting = listen
  table.insert(self.raw_hosts, host)
end

function Server:get_binding(id)
  if self ~= Server then --allow Server.get_binding() invocation
    id = self
  end
  if type(id) == "userdata" then
    local binding = Server.bindings_by_ptr[id]
    if not binding then --create lua data from C struct
      binding = CFuncs.create_binding_table_from_ptr(id)
      table.insert(Server.bindings, binding)
      Server.bindings_by_ptr[id] = binding
    end
    return binding
  end
end

local function common_parent_block(blocks)
  local parents = {}
  local maxlen = 0
  if #blocks == 1 then
    return blocks[1]
  end
  for _, block in pairs(blocks) do
    local cur = block
    local pchain = {}
    repeat
      table.insert(pchain, 1, cur)
      cur = cur:parent_block()
    until not cur or cur.name == "::ROOT"
    
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
  return nil
end

local function publish_server_started_event(ok)
  Server:publish("start", true)
  Server:publish(Process.type()..".start", true)
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
      for _, addr in ipairs(addrs) do
        addr.type = host.socket_type
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
        local name
        if addr.address then
          name = addr.port and (addr.address ..":"..addr.port) or addr.address
          id = (host.socket_type or "") ..":".. (host.server_type or "") .. ":" ..  name
        elseif addr.path then
          id = addr.path
          name = addr.path
        else
          return nil, host.setting:error("can't figure out internal id")
        end
        unique_bindings[id]=unique_bindings[id] or {address = addr, listen = {}}
        table.insert(unique_bindings[id].listen, {
          name = name,
          block = host.block,
          setting = host.setting,
          server_type = host.server_type,
          socket_type = host.type or host.socket_type or "TCP"
        })
        unique_binding_blocks[id] = unique_binding_blocks[id] or {}
        table.insert(unique_binding_blocks[id], host.block)
      end
    end
    Server.bindings = {}
    for id, binding in pairs(unique_bindings) do
      binding.common_parent_block = assert(common_parent_block(unique_binding_blocks[id]))
      table.insert(Server.bindings, binding)
      
      local host_server_type, host_binding_name
      for _, host in pairs(binding.listen) do
        host_binding_name = host.name
        if not host_server_type then
          host_server_type = host.server_type
        elseif host_server_type ~= host.server_type then
          return nil, host.setting:error("can't listen on the same address as server type '" .. host_server_type .. "'")
        end
      end
      binding.name = host_binding_name
      binding.server_type = host_server_type
    end
    
    --for _, binding in pairs(Server.bindings) do
    --
    --end
    
    local worker_procnums = Process.worker_procnums()
    if #Server.bindings > 0 then
      
      local rcv = IPC.Receiver.start("server:create_listener_sockets", "master")
      local rcvfd = IPC.FD_Receiver.start("server:receive_listener_sockets", 5.0)
      
      for i, binding in ipairs(Server.bindings) do
        
        binding.ptr = assert(CFuncs.create_binding_data(binding, i), "problem creating bind data")
        Server.bindings_by_ptr[binding.ptr]=binding
        
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
  IPC.receive("server:workers_started", "any", function(ok)
    IPC.send("all", "server:start", ok or true)
  end)
  IPC.receive("server:start", "manager", publish_server_started_event)
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
        local c_io_coro = CFuncs.start_worker_io_listener_coro(Server, data.fd, data.binding_ptr)
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
    Server.shared:increment("done", 1)
    if Server.shared.done == Process.count_workers() then
      IPC.send("manager", "server:workers_started", true)
    end
  end)
  
  IPC.receive("server:start", "manager", publish_server_started_event)
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
  IPC.receive("server:start", "manager", publish_server_started_event)
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
