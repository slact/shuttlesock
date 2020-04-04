#include <shuttlesock.h>
#include <lualib.h>
#include <lauxlib.h>
#include "server.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

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
  
  lua_pushvalue(L, 1);
  binding->ref = luaL_ref(L, LUA_REGISTRYINDEX);
  
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
  
  lua_getfield(L, -1, "common_parent_block");
  if(!lua_isnil(L, -1)) {
    lua_getfield(L, -1, "ptr");
    if(lua_islightuserdata(L, -1) || lua_isuserdata(L, -1)) {
      binding->config.common_parent_block = (void *)lua_topointer(L, -1);
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
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
  
  
  shuso_event_init_t evinit = {
    .name = "accept",
    .event = &binding->accept_event,
    .data_type = "server_accept", 
  };
  shuso_event_initialize(S, shuso_get_module(S, "server"), &binding->accept_event, &evinit);
  
  lua_pushlightuserdata(L, binding);
  return 1;
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

static void free_shared_host_data(shuso_t *S, shuso_hostinfo_t *h) {
  if(h) {
    if(h->sockaddr) {
      shuso_shared_slab_free(&S->common->shm, h->sockaddr);
    }
    if(h->addr_family == AF_UNIX && h->path) {
      shuso_shared_slab_free(&S->common->shm, h->path);
    }
    shuso_shared_slab_free(&S->common->shm, h);
  }
}

static int luaS_create_shared_host_data(lua_State *L) {
  shuso_t *S = shuso_state(L);
  shuso_server_binding_t   *binding = (void *)lua_topointer(L, 1);
  shuso_hostinfo_t *host = &binding->host;
  
  shuso_hostinfo_t *shared_host = shuso_shared_slab_alloc(&S->common->shm, sizeof(*shared_host));
  if(!shared_host) {
    goto fail;
  }
  *shared_host = *host;
  
  if(host->name) {
    shared_host->name = shuso_shared_slab_alloc(&S->common->shm, strlen(host->name)+1);
    if(!shared_host->name) {
      goto fail;
    }
    strcpy((char *)shared_host->name, host->name);
  }
  if(host->addr_family == AF_UNIX && host->path) {
    shared_host->path = shuso_shared_slab_alloc(&S->common->shm, strlen(host->path)+1);
    if(!shared_host->path) {
      goto fail;
    }
    strcpy((char *)shared_host->path, host->path);
  }
  
  if(host->sockaddr) {
    if(host->sockaddr->sa_family == AF_UNIX) {
      shared_host->sockaddr_un = shuso_shared_slab_alloc(&S->common->shm, sizeof(*host->sockaddr_un));
      if(!shared_host->sockaddr_un) {
        goto fail;
      }
      *shared_host->sockaddr_un = *host->sockaddr_un;
    }
    else if(host->sockaddr->sa_family == AF_INET) {
      shared_host->sockaddr_in = shuso_shared_slab_alloc(&S->common->shm, sizeof(*host->sockaddr_in));
      if(!shared_host->sockaddr_in) {
        goto fail;
      }
      *shared_host->sockaddr_in = *host->sockaddr_in;
    }
#ifdef SHUTTLESOCK_HAVE_IPV6
    else if(host->sockaddr->sa_family == AF_INET6) {
      shared_host->sockaddr_in6 = shuso_shared_slab_alloc(&S->common->shm, sizeof(*host->sockaddr_in6));
      if(!shared_host->sockaddr_in6) {
        goto fail;
      }
      *shared_host->sockaddr_in6 = *host->sockaddr_in6;
    }
#endif
  }
  
  lua_pushlightuserdata(L, shared_host);
  return 1;
  
fail:
  free_shared_host_data(S, shared_host);
  return luaL_error(L, "Failed to allocate shared memory for fd request");
}

static int luaS_handle_fd_request(lua_State *L) {
  shuso_hostinfo_t *hostinfo = (void *)lua_topointer(L, 1);
  shuso_t          *S = shuso_state(L);
  int               socktype = hostinfo->udp ? SOCK_DGRAM : SOCK_STREAM;
  union {
    struct sockaddr        sa;
    struct sockaddr_un     sa_unix;
    struct sockaddr_in     sa_inet;
#ifdef SHUTTLESOCK_HAVE_IPV6
    struct sockaddr_in6    sa_inet6;
#endif
  }                 sockaddr;
  size_t            sockaddr_len = sizeof(sockaddr);
  
  shuso_sockopt_t sopt = {
    .level = SOL_SOCKET,
    .name = SO_REUSEPORT,
    .value.integer = 1
  };
  shuso_sockopts_t opts = {
    .count = 1,
    .array = &sopt
  };
  
  assert(S->procnum == SHUTTLESOCK_MASTER);
  
  if(!shuso_hostinfo_to_sockaddr(S, hostinfo, &sockaddr.sa, &sockaddr_len)) {
    lua_pushnil(L);
    lua_pushfstring(L, "Failed to obtain sockaddr for listener socket %s", hostinfo->name ? hostinfo->name : "");
    return 2;
  }
  
  int fd = socket(hostinfo->addr_family, socktype, 0);
  if(fd == -1) {
    lua_pushnil(L);
    lua_pushfstring(L, "Failed to create listener socket %s: %s", hostinfo->name ? hostinfo->name : "", strerror(errno));
    return 2;
  }
  
  shuso_set_nonblocking(fd);
  
  if(bind(fd, &sockaddr.sa, sockaddr_len) == -1) {
    close(fd);
    lua_pushnil(L);
    lua_pushfstring(L, "Failed to bind listener socket %s: %s", hostinfo->name ? hostinfo->name : "", strerror(errno));
    return 2;
  }
  
  
  for(unsigned i=0; i < opts.count; i++) {
    shuso_sockopt_t *opt = &opts.array[i];
    if(!shuso_setsockopt(S, fd, opt)) {
      close(fd);
      lua_pushnil(L);
      lua_pushfstring(L, "Failed to set sockopts for listener socket %s: %s", hostinfo->name ? hostinfo->name : "", strerror(errno));
      return 2;
    }
  }
  
  lua_pushinteger(L, fd);
  return 1;
}

static int luaS_free_shared_host_data(lua_State *L) {
  shuso_hostinfo_t *shared_host = (void *)lua_topointer(L, 1);
  free_shared_host_data(shuso_state(L), shared_host);
  lua_pushboolean(L, 1);
  return 1;
}

static bool binding_data_lua_wrap(lua_State *L, const char *type, void *data) {
  assert(strcmp(type, "server_binding") == 0);
  shuso_server_binding_t *binding = data;
  lua_checkstack(L, 1);
  lua_rawgeti(L, LUA_REGISTRYINDEX, binding->ref);
  return true;
}

static lua_reference_t binding_data_lua_unwrap(lua_State *L, const char *type, int narg, void **ret) {
  assert(strcmp(type, "server_binding") == 0);
  lua_checkstack(L, 1);
  lua_getfield(L, narg, "ptr");
  assert(lua_islightuserdata(L, -1) || lua_isuserdata(L, -1));
  *ret = (void *)lua_topointer(L, -1);
  lua_pop(L, 1);
  return LUA_NOREF;
}

void shuttlesock_server_module_prepare(shuso_t *S, void *pd) {
  luaL_Reg lib[] = {
    {"getaddrinfo_noresolve", get_addr_info},
    {"create_binding_data", luaS_create_binding_data},
    {"start_worker_io_listener_coro", luaS_start_worker_io_listener_coroutine},
    {"stop_worker_io_listener_coro", luaS_stop_worker_io_listener_coroutine},
    
    {"create_shared_host_data", luaS_create_shared_host_data},
    {"handle_fd_request", luaS_handle_fd_request},
    {"free_shared_host_data", luaS_free_shared_host_data},
    {NULL, NULL}
  };
  
  luaS_register_lib(S->lua.state, "shuttlesock.modules.core.server.cfuncs", lib);
  
  shuso_lua_event_register_data_wrapper(S, "server_binding", &(shuso_lua_event_data_wrapper_t ){
    .wrap =           binding_data_lua_wrap,
    .unwrap =         binding_data_lua_unwrap
  });
  
}
