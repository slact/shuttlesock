#include <shuttlesock.h>
#include <lualib.h>
#include <lauxlib.h>
#include "server.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

static int get_addr_info(lua_State *L) {
  const char    *ip_str = luaL_checkstring(L, 1);
  bool           ipv4 = false, ipv6 = false;
  if(lua_checkstack(L, 2)) {
    const char *str = lua_tostring(L, 2);
    if(str) {
      if(strcasecmp(str, "ipv4") == 0) {
        ipv4 = true;
      }
      if(strcasecmp(str, "ipv6") == 0) {
        ipv6 = true;
      }
    }
    else if(lua_isnumber(L, 2)) {
      int ipnum = lua_tonumber(L, 2);
      if(ipnum == 4) {
        ipv4 = true;
      }
      else if(ipnum == 6) {
        ipv6 = true;
      }
    }
  }
  
  struct addrinfo hints = {
    .ai_flags = AI_NUMERICHOST | AI_PASSIVE,
  };
  
  if(ipv4) {
    hints.ai_family = AF_INET;
  }
  if(ipv6) {
#ifndef SHUTTLESOCK_HAVE_IPV6
    lua_pushnil(L);
    lua_pushstring(L, "IPv6 Not supported");
    return 2;
#else
    hints.ai_family = AF_INET6;
#endif
  }
  if(!ipv4 && !ipv6) {
    hints.ai_family = AF_UNSPEC;
  }
  
  struct addrinfo *res = NULL;
  int rc = getaddrinfo(ip_str, NULL, &hints, &res);
  
  if(rc != 0) {
    if(res) {
      freeaddrinfo(res);
    }
    lua_pushnil(L);
    lua_pushstring(L, gai_strerror(rc));
    return 2;
  }
  
  lua_newtable(L);
  
  int i = 1;
  
  for(struct addrinfo *rp = res; rp != NULL; rp = rp->ai_next) {
    
    assert(rp->ai_addr->sa_family == rp->ai_family);
    
    lua_newtable(L);
    
    switch(rp->ai_socktype) {
      case SOCK_STREAM:
        lua_pushliteral(L, "stream");
        break;
      case SOCK_DGRAM:
        lua_pushliteral(L, "dgram");
        break;
      case SOCK_SEQPACKET:
        lua_pushliteral(L, "seqpacket");
        break;
      case SOCK_RAW:
        lua_pushliteral(L, "raw");
        break;
      case SOCK_RDM:
        lua_pushliteral(L, "rdm");
        break;
      default:
        lua_pushliteral(L, "unknown");
        break;
    }
    lua_setfield(L, -2, "socktype");
    
    if(rp->ai_family == AF_INET) {
      lua_pushliteral(L, "IPv4");
      lua_setfield(L, -2, "family");
      
      struct sockaddr_in *sa = (struct sockaddr_in *)rp->ai_addr;
      lua_pushlstring(L, (char *)&sa->sin_addr.s_addr, sizeof(sa->sin_addr.s_addr));
      lua_setfield(L, -2, "address_binary");
      
      char  address_str[INET_ADDRSTRLEN];
      if(inet_ntop(AF_INET, (char *)&sa->sin_addr.s_addr, address_str, sizeof(address_str)) == NULL) {
        freeaddrinfo(res);
        return luaL_error(L, "inet_ntop failed on address in list. this is very weird");
      }
      lua_pushstring(L, address_str);
      lua_setfield(L, -2, "address");
    }
#ifdef SHUTTLESOCK_HAVE_IPV6
    else if(rp->ai_family == AF_INET6) {
      lua_pushliteral(L, "IPv6");
      lua_setfield(L, -2, "family");
      
      struct sockaddr_in6 *sa = (struct sockaddr_in6 *)rp->ai_addr;
      lua_pushlstring(L, (char *)sa->sin6_addr.s6_addr, sizeof(sa->sin6_addr.s6_addr));
      lua_setfield(L, -2, "address_binary");
      
      char  address_str[INET6_ADDRSTRLEN];
      if(inet_ntop(AF_INET6, (char *)sa->sin6_addr.s6_addr, address_str, sizeof(address_str)) == NULL) {
        freeaddrinfo(res);
        return luaL_error(L, "inet_ntop failed on address in list. this is very weird");
      }
      lua_pushstring(L, address_str);
      lua_setfield(L, -2, "address");
    }
#endif
    
    lua_seti(L, -2, i);
    i++;
  }
  freeaddrinfo(res);
  return 1;
}

static int luaS_create_binding_data(lua_State *L) {
  shuso_t                  *S = shuso_state(L);
  shuso_server_binding_t   *binding = shuso_stalloc(&S->stalloc, sizeof(*binding));
  const char               *name;
  
  if(!binding) {
    return luaL_error(L, "failed to allocate memory for server binding");
  }
  binding->lua_hostnum = lua_tointeger(L, 2);
  
  lua_getfield(L, 1, "name");
  name = lua_tostring(L, -1);
  lua_pop(L, 1); //pop ["name"]
  
  lua_getfield(L, 1, "address");
  
  lua_getfield(L, -1, "family");
  const char *famstr = lua_tostring(L, -1);
  int fam = AF_UNSPEC;
  if(famstr) {
    if(strcasecmp(famstr, "IPv4") == 0) {
      fam = AF_INET;
    }
#ifdef SHUTTLESOCK_HAVE_IPV6
    else if(strcasecmp(famstr, "IPv6") == 0) {
      fam = AF_INET6;
    }
#endif
    else if(strcasecmp(famstr, "unix") == 0) {
      fam = AF_UNIX;
    }
  }
  lua_pop(L, 1); // pop ['family']
  
  binding->host.addr_family = fam;
  
  union {
    struct sockaddr_un     sa_unix;
    struct sockaddr_in     sa_inet;
#ifdef SHUTTLESOCK_HAVE_IPV6
    struct sockaddr_in6    sa_inet6;
#endif
  }                     sockaddr;
  
  if(fam == AF_UNIX) {
    lua_getfield(L, -1, "path");
    binding->host.path = lua_tostring(L, -1);
    //no need to ref this string -- it's ref'd in the Lua structure
    lua_pop(L, 1); //pop ["path"]
  }
  else {
    lua_getfield(L, -1, "port");
    binding->host.port = lua_tointeger(L, -1);
    lua_pop(L, 1); //pop "port"
    
    lua_getfield(L, -1, "address_binary");
    const char *addr;
    size_t      addrlen;
    addr = lua_tolstring(L, -1, &addrlen);
    lua_pop(L, 1); //pop ["address_binary"]
    
    if(fam == AF_INET) {
      assert(sizeof(binding->host.addr) == addrlen);
      memcpy(&binding->host.addr, addr, addrlen);
    }
#ifdef SHUTTLESOCK_HAVE_IPV6
    else if(fam == AF_INET6) {
      assert(sizeof(binding->host.addr) == addrlen);
      memcpy(&binding->host.addr6, addr, addrlen);
    }
    else {
      return luaL_error(L, "weird address family");
    }
#endif
  }
  
  shuso_hostinfo_t *host = &binding->host;
  
  host->name = name;
  
  host->sockaddr = shuso_stalloc(&S->stalloc, sizeof(sockaddr));
  if(!host->sockaddr) {
    return luaL_error(L,"couldn't allocate host sockaddr");
  }
  if(fam == AF_UNIX) {
    host->sockaddr_un->sun_family = AF_UNIX;
    size_t pathlen = strlen(host->path)+1, maxlen = sizeof(host->sockaddr_un->sun_path);
    memcpy(host->sockaddr_un->sun_path, host->path, pathlen > maxlen ? maxlen : pathlen);
  }
  else if(fam == AF_INET) {
    printf("!!! host sockaddr: %p %p\n", host->sockaddr_in, &host->sockaddr_in[1]);
    host->sockaddr_in->sin_family = AF_INET;
    host->sockaddr_in->sin_port = htons(host->port);
    host->sockaddr_in->sin_addr = host->addr;
  }
#ifdef SHUTTLESOCK_HAVE_IPV6
  else if(fam == AF_INET6) {
    host->sockaddr_in6->sin6_family = AF_INET6;
    host->sockaddr_in6->sin6_port = htons(host->port);
    host->sockaddr_in6->sin6_addr = host->addr6;
  }
#endif
  lua_pop(L, 1); //pop ["address"]

  lua_getfield(L, 1, "listen");
  int listen_count = luaL_len(L, -1);
  binding->config.count = listen_count;
  binding->config.array = shuso_stalloc(&S->stalloc, sizeof(*binding->config.array) * listen_count);
  if(!binding->config.array) {
    luaL_error(L, "couldn't allocate server host config array");
  }
  
  for(int j=0; j<listen_count; j++) {
    
    lua_geti(L, -1, j+1);
    
    lua_getfield(L, -1, "block");
    
    lua_getfield(L, -1, "ptr");
    binding->config.array[j].block = (void *)lua_topointer(L, -1);
    lua_pop(L, 2); //pop .block.ptr
    
    lua_getfield(L, -1, "setting");
    lua_getfield(L, -1, "ptr");
    binding->config.array[j].block = (void *)lua_topointer(L, -1);
    lua_pop(L, 2); //pop .setting.ptr
    
    lua_pop(L, 1); //pop listen[i]
  }
  
  lua_pop(L, 1); //pop ["listen"]
  
  lua_pushlightuserdata(L, binding);
  lua_setfield(L, 1, "ptr");
  
  lua_pushboolean(L, 1);
  return 1;
}

static void luaS_get_listen_fds_coro_resume(shuso_t *S, shuso_status_t status, shuso_hostinfo_t *hostinfo, int *sockets, int socket_count, void *pd) {
  lua_State       *L = S->lua.state;
  lua_reference_t  luaref = (intptr_t )pd;
  
  lua_rawgeti(L, LUA_REGISTRYINDEX, luaref);
  lua_State *coro = lua_tothread(L, -1);
  lua_pop(L, 1);
  luaL_unref(L, LUA_REGISTRYINDEX, luaref);
  
  
  if(status != SHUSO_OK) {
    lua_pushnil(L);
  }
  else {
    lua_newtable(coro);
    for(int i=0; i<socket_count; i++) {
      lua_pushinteger(coro, sockets[i]);
      lua_seti(coro, -2, i+1);
    }
  }
  luaS_resume(coro, L, 1);
}

static int luaS_get_listen_fds_coro_yield(lua_State *L) {
  shuso_t                *S = shuso_state(L);
  shuso_server_binding_t *binding = (void *)lua_topointer(L, 1);
  
  shuso_sockopt_t sopt = {
    .level = SOL_SOCKET,
    .name = SO_REUSEPORT,
    .value.integer = 1
  };
  shuso_sockopts_t opts = {
    .count = 1,
    .array = &sopt
  };
  
  lua_pushthread(L); //current coro
  lua_reference_t coro_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  
  if(!shuso_ipc_command_open_listener_sockets(S, &binding->host, shuso_workers_count(S), &opts, luaS_get_listen_fds_coro_resume, (void *)(intptr_t)coro_ref)) {
    return luaL_error(L, "shuso_ipc_command_open_listener_sockets failed");
  }
  
  lua_pushboolean(L, 1);
  return lua_yield(L, 1);
}

static void listener_accept_coro_error(shuso_t *S, shuso_io_t *io) {
  shuso_log(S, "socket accept listener error");
}

static void listener_accept_coro(shuso_t *S, shuso_io_t *io) {
  int rc = 0;
  SHUSO_IO_CORO_BEGIN(io, listener_accept_coro_error);
  rc = listen(io->io_socket.fd, 100);
  if(rc < 0) {
    shuso_set_error(S, "failed to listen on that ol' socket");
  }
  while(rc == 0) {
    SHUSO_IO_CORO_YIELD(wait, SHUSO_IO_READ);
    SHUSO_IO_CORO_YIELD(accept);
    shuso_log(S, "accepted new socket");
  }
  SHUSO_IO_CORO_END(io);
}

static int luaS_start_worker_io_listener_coroutine(lua_State *L) {
  shuso_t *S = shuso_state(L);
  shuso_io_t *io = shuso_stalloc(&S->stalloc, sizeof(*io));
  if(!io) {
    return luaL_error(L, "failed to allocate shuso_io for listener socket");
  }
  int                 fd = lua_tointeger(L, 1);
  shuso_hostinfo_t   *host = (void *)lua_topointer(L, 2);
  shuso_socket_t      sock = {
    .fd = fd,
    .host = *host
  };
  
  shuso_io_init(S, io, &sock, SHUSO_IO_READ, listener_accept_coro, NULL);
  shuso_io_start(io);
  
  lua_pushlightuserdata(L, io);
  return 1;
}

static int luaS_stop_worker_io_listener_coroutine(lua_State *L) {
  shuso_io_t *io = (void *)lua_topointer(L, 1);
  shuso_io_stop(io);
  close(io->io_socket.fd);
  lua_pushboolean(L, 1);
  return 1;
}

void shuttlesock_server_module_prepare(shuso_t *S, void *pd) {
  luaL_Reg lib[] = {
    {"getaddrinfo_noresolve", get_addr_info},
    {"get_listen_fds_yield", luaS_get_listen_fds_coro_yield},
    {"create_binding_data", luaS_create_binding_data},
    {"start_worker_io_listener_coro", luaS_start_worker_io_listener_coroutine},
    {"stop_worker_io_listener_coro", luaS_stop_worker_io_listener_coroutine},
    {NULL, NULL}
  };
  
  luaS_register_lib(S->lua.state, "shuttlesock.modules.core.server.cfuncs", lib);
}
