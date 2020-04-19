#include <shuttlesock.h>
#include <lualib.h>
#include <lauxlib.h>
#include "server.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

static int luaS_create_binding_data(lua_State *L) {
  shuso_t                  *S = shuso_state(L);
  shuso_server_binding_t   *binding = shuso_stalloc(&S->stalloc, sizeof(*binding));
  const char               *name;
  
  if(!binding) {
    return luaL_error(L, "failed to allocate memory for server binding");
  }
  binding->lua_hostnum = lua_tointeger(L, 2);
  
  lua_getfield(L, 1, "type");
  const char *server_type = lua_tostring(L, -1);
  binding->server_type = shuso_stalloc(&S->stalloc, strlen(server_type)+1);
  strcpy((char *)binding->server_type, server_type);
  lua_pop(L, 1);
  
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
    return luaL_error(L, "couldn't allocate server host config array");
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
  
  lua_pushlightuserdata(L, binding);
  return 1;
}

typedef struct {
  shuso_io_t                io;
  shuso_event_t            *maybe_accept_event;
  shuso_event_t            *accept_event;
  shuso_server_binding_t   *binding;
} shuso_listener_io_data_t;

static void listener_accept_coro_error(shuso_t *S, shuso_io_t *io) {
  shuso_log(S, "socket accept listener error");
}

static void listener_accept_coro(shuso_t *S, shuso_io_t *io) {
  
  shuso_listener_io_data_t   *d = io->privdata;
  shuso_server_tentative_accept_data_t  maybe_accept_data;
  int rc = 0;
  SHUSO_IO_CORO_BEGIN(io, listener_accept_coro_error);
  rc = listen(io->io_socket.fd, 100);
  if(rc < 0) {
    shuso_set_error_errno(S, "failed to listen on %s: %s", d->binding->host.name ? d->binding->host.name : "(?)", strerror(errno));
  }
  else {
    shuso_log_info(S, "Listening on %s", d->binding->host.name ? d->binding->host.name : "(?)");
  }
  while(rc == 0) {
    shuso_log_debug(S, "WAIT_READ");
    SHUSO_IO_CORO_YIELD(wait, SHUSO_IO_READ);
    shuso_log_debug(S, "accept()");
    SHUSO_IO_CORO_YIELD(accept);
    shuso_log_debug(S, "accepted");
    memcpy(&maybe_accept_data.sockaddr, &io->sockaddr, sizeof(io->sockaddr));
    maybe_accept_data.fd = io->result_fd;
    maybe_accept_data.binding = d->binding;
    maybe_accept_data.accept_event = d->accept_event;
    
    shuso_event_publish(S, d->maybe_accept_event, SHUSO_OK, &maybe_accept_data);
  }
  SHUSO_IO_CORO_END(io);
}

static int luaS_start_worker_io_listener_coroutine(lua_State *L) {
  shuso_t *S = shuso_state(L);
  shuso_listener_io_data_t  *data;
  int                        fd = lua_tointeger(L, 2);
  shuso_server_binding_t    *binding = (void *)lua_topointer(L, 3);
  assert(binding);
  
  data = shuso_stalloc(&S->stalloc, sizeof(*data));
  if(!data) {
    return luaL_error(L, "failed to allocate shuso_io data for listener socket");
  }
  
  data->binding = binding;
  lua_getfield(L, 1, "event_pointer");
  lua_pushvalue(L, 1);
  lua_pushliteral(L, "maybe_accept");
  luaS_call(L, 2, 1);
  data->maybe_accept_event = (void *)lua_topointer(L, -1);
  assert(data->maybe_accept_event);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "event_pointer");
  lua_pushvalue(L, 1);
  lua_pushfstring(L, "%s.accept", binding->server_type);
  luaS_call(L, 2, 1);
  data->accept_event = (void *)lua_topointer(L, -1);
  assert(data->accept_event);
  lua_pop(L, 1);
  
  shuso_socket_t      sock = {
    .fd = fd,
    .host = binding->host
  };
  
  shuso_io_init(S, &data->io, &sock, SHUSO_IO_READ, listener_accept_coro, data);
  shuso_io_start(&data->io);
  
  lua_pushlightuserdata(L, &data->io);
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
  for(unsigned i=0; i < opts.count; i++) {
    shuso_sockopt_t *opt = &opts.array[i];
    if(!shuso_setsockopt(S, fd, opt)) {
      close(fd);
      lua_pushnil(L);
      lua_pushfstring(L, "Failed to set sockopts for listener socket %s: %s", hostinfo->name ? hostinfo->name : "", strerror(errno));
      return 2;
    }
  }
  
  
  if(bind(fd, &sockaddr.sa, sockaddr_len) == -1) {
    close(fd);
    lua_pushnil(L);
    lua_pushfstring(L, "Failed to bind listener socket %s: %s", hostinfo->name ? hostinfo->name : "", strerror(errno));
    return 2;
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

static void maybe_accept_event_confirm_accept(shuso_t *S, shuso_event_state_t *evs, intptr_t code,  void *d, void *pd) {
  shuso_server_tentative_accept_data_t *data = d;
  shuso_server_accept_data_t            accept_data;
  shuso_socket_t                        socket;
  
  socket.fd = data->fd;
  socket.host.name = NULL;
  
  //TODO: udp flag from binding
  
  switch(data->sockaddr.any.sa_family) {
    case AF_INET:
      socket.host.addr_family = AF_INET;
      socket.host.addr = data->sockaddr.inet.sin_addr;
      socket.host.port = data->sockaddr.inet.sin_port;
      socket.host.sockaddr_in = &data->sockaddr.inet;
      break;
#ifdef SHUTTLESOCK_HAVE_IPV6
    case AF_INET6:
      socket.host.addr_family = AF_INET6;
      socket.host.addr6 = data->sockaddr.inet6.sin6_addr;
      socket.host.port = data->sockaddr.inet6.sin6_port;
      socket.host.sockaddr_in6 = &data->sockaddr.inet6;
      break;
#endif
    case AF_UNIX:
      socket.host.addr_family = AF_UNIX;
      //TODO: get the unix path
      socket.host.path = NULL;
      socket.host.port = 0;
      socket.host.sockaddr_un = NULL;
      break;
  }
  
  accept_data = (shuso_server_accept_data_t ) {
    .socket = &socket,
    .binding = data->binding
  };
  
  shuso_event_publish(S, data->accept_event, SHUSO_OK, &accept_data);
}

static void no_accept_handler_found(shuso_t *S, shuso_event_state_t *evs, intptr_t code,  void *d, void *pd) {
  shuso_server_accept_data_t *data = d;
  close(data->socket->fd);
  //TODO: from where?
  shuso_log_info(S, "No handler accepted incoming connection [FROM WHERE?]");
}

static int luaS_accept_event_init(lua_State *L) {
  shuso_t *S = shuso_state(L);
  
  shuso_event_t *accept_event = (void *)lua_topointer(L, 1);
  shuso_event_listen_with_priority(S, accept_event, no_accept_handler_found, NULL, SHUTTLESOCK_LAST_PRIORITY);
  
  return 0;
}

static int luaS_maybe_accept_event_init(lua_State *L) {
  shuso_t *S = shuso_state(L);
  
  shuso_event_t *maybe_accept_event = (void *)lua_topointer(L, 1);
  shuso_event_listen_with_priority(S, maybe_accept_event, maybe_accept_event_confirm_accept, NULL, SHUTTLESOCK_LAST_PRIORITY);
  
  return 0;
}

static bool lua_event_data_socket_wrap(lua_State *L, const char *type, void *data) {
  assert(strcmp(type, "shuso_socket") == 0);
  
  const shuso_socket_t *sock = data;
  
  int top = lua_gettop(L);
  lua_checkstack(L, 3);
  
  lua_newtable(L);
  
  lua_pushinteger(L, sock->fd);
  lua_setfield(L, -2, "fd");
  
  if(sock->host.name) {
    lua_pushstring(L, sock->host.name);
    lua_setfield(L, -2, "name");
  }
  
  lua_pushinteger(L, sock->host.port);
  lua_setfield(L, -2, "port");
  
  if(sock->host.udp) {
    lua_pushliteral(L, "dgram");
    lua_setfield(L, -2, "socktype");
    
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "UDP");
  }
  else {
    lua_pushliteral(L, "stream");
    lua_setfield(L, -2, "socktype");
    if(sock->host.addr_family != AF_UNIX) {
      lua_pushboolean(L, 1);
      lua_setfield(L, -2, "TCP");
    }
  }
  
  switch(sock->host.addr_family) {
    case AF_INET: {
      lua_pushliteral(L, "IPv4");
      lua_setfield(L, -2, "family");
      
      lua_pushlstring(L, (char *)&sock->host.addr.s_addr, sizeof(sock->host.addr.s_addr));
      lua_setfield(L, -2, "address_binary");
      
      char  address_str[INET_ADDRSTRLEN];
      if(inet_ntop(AF_INET, (char *)&sock->host.addr, address_str, INET_ADDRSTRLEN)) {
        lua_pushstring(L, address_str);
        lua_setfield(L, -2, "address");
      }
      
      break;
    }
#ifdef SHUTTLESOCK_HAVE_IPV6
    case AF_INET6: {
      lua_pushliteral(L, "IPv6");
      lua_setfield(L, -2, "family");
      
      lua_pushlstring(L, (char *)&sock->host.addr6.s6_addr, sizeof(sock->host.addr6.s6_addr));
      lua_setfield(L, -2, "address_binary");
      char address_str[INET6_ADDRSTRLEN];
      if(inet_ntop(AF_INET6, (char *)&sock->host.addr6, address_str, INET6_ADDRSTRLEN)) {
        lua_pushstring(L, address_str);
        lua_setfield(L, -2, "address");
      }
      
      break;
    }
#endif
    case AF_UNIX:
      lua_pushliteral(L, "unix");
      lua_setfield(L, -2, "family");
      
      lua_pushstring(L, sock->host.path);
      lua_setfield(L, -2, "path");
      break;
  }
  lua_setfield(L, -2, "family");
  
  assert(lua_gettop(L) == top+1);
  
  return true;
}
static bool lua_event_data_socket_wrap_cleanup(lua_State *L, const char *type, void *data) {
  assert(strcmp(type, "shuso_socket") == 0);
  return true;
}
static lua_reference_t lua_event_data_socket_unwrap(lua_State *L, const char *type, int idx, void **ret) {
  assert(strcmp(type, "shuso_socket") == 0);
  int top = lua_gettop(L);
  
  lua_checkstack(L, 3);
  
  shuso_socket_t *sock;
  
  lua_getfield(L, idx, "family");
  sa_family_t fam;
  if(luaS_streq_literal(L, -1, "IPv4") || luaS_streq_literal(L, -1, "ipv4")) {
    struct {
      shuso_socket_t        sock;
      struct sockaddr_in    addr;
    } *sockblob;
    
    sockblob = lua_newuserdata(L, sizeof(*sockblob));
    sockblob->sock.host.sockaddr_in = &sockblob->addr;
    sock = &sockblob->sock;
    fam = AF_INET;
  }
  else if(luaS_streq_literal(L, -1, "IPv6") || luaS_streq_literal(L, -1, "ipv6")) {
#ifndef SHUTTLESOCK_HAVE_IPV6
    fam = 0;
    sock = lua_newuserdata(L, sizeof(*sock));
    sock->host.sockaddr = NULL;
#else
    struct {
      shuso_socket_t        sock;
      struct sockaddr_in6   addr;
    } *sockblob;
    
    sockblob = lua_newuserdata(L, sizeof(*sockblob));
    sockblob->sock.host.sockaddr_in6 = &sockblob->addr;
    sock = &sockblob->sock;
    fam = AF_INET6;
#endif
  }
  else if(luaS_streq_literal(L, -1, "Unix") || luaS_streq_literal(L, -1, "unix") || luaS_streq_literal(L, -1, "local") || luaS_streq_literal(L, -1, "UNIX")) {
    struct {
      shuso_socket_t        sock;
      struct sockaddr_un    addr;
    } *sockblob;
    
    sockblob = lua_newuserdata(L, sizeof(*sockblob));
    sockblob->sock.host.sockaddr_un = &sockblob->addr;
    sock = &sockblob->sock;
    fam = AF_UNIX;
  }
  else {
    fam = 0;
    sock = lua_newuserdata(L, sizeof(*sock));
    sock->host.sockaddr = NULL;
  }
  int ref = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_pop(L, 1); //["family"]
  
  lua_getfield(L, idx, "fd");
  sock->fd = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : -1;
  lua_pop(L, 1);
  
  lua_getfield(L, idx, "name");
  sock->host.name = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
  lua_pop(L, 1);
  
  lua_getfield(L, idx, "port");
  sock->host.port = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : 0;
  lua_pop(L, 1);
  
  size_t        sz;
  const char   *str;
  
  switch(fam) {
    case AF_INET: {
      sock->host.addr_family = AF_INET;
      sock->host.sockaddr_in->sin_family = AF_INET;
      sock->host.sockaddr_in->sin_port = htons(sock->host.port);
      
      lua_getfield(L, idx, "address_binary");
      str = lua_tolstring(L, -1, &sz);
      sz = sizeof(sock->host.addr.s_addr) > sz ? sz : sizeof(sock->host.addr.s_addr);
      memcpy(&sock->host.addr.s_addr, str, sz);
      memcpy(&sock->host.sockaddr_in->sin_addr.s_addr, str, sz);
      lua_pop(L, 1);
      
      break;
    }
#ifndef SHUTTLESOCK_HAVE_IPV6    
    case AF_INET6: {
      sock->host.addr_family = AF_INET6;
      sock->host.sockaddr_in6->sin6_family = AF_INET6;
      sock->host.sockaddr_in6->sin6_port = htons(sock->host.port);
      sock->host.sockaddr_in6->sin6_flowinfo = 0;
      //sock->sockaddr_in6->sin6_scope_id = 0; /* Scope ID (new in 2.4) */ //is this even used?...
      
      lua_getfield(L, idx, "address_binary");
      str = lua_tolstring(L, -1, &sz);
      sz = sizeof(sock->host.addr6.s6_addr) > sz ? sz : sizeof(sock->host.addr6.s6_addr);
      memcpy(&sock->host.addr6.s6_addr, str, sz);
      memcpy(&sock->host.sockaddr_in6->sin6_addr.s6_addr, str, sz);
      lua_pop(L, 1);
      
      break;
    }
#endif
    case AF_UNIX:
      
      lua_getfield(L, idx, "path");
      sock->host.path = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
      lua_pop(L, 1);
      
      sock->host.sockaddr_un->sun_family = AF_UNIX;
      if(sock->host.path) {
        strncpy(sock->host.sockaddr_un->sun_path, sock->host.path, sizeof(sock->host.sockaddr_un->sun_path));
      }
      else {
        sock->host.sockaddr_un->sun_path[0] = '\0';
      }
      break;
      
    default:
      sock->host.addr_family = 0;
      break;
  }
  
  assert(top == lua_gettop(L));
  return ref;
}
static bool lua_event_data_socket_unwrap_cleanup(lua_State *L, const char *type, lua_reference_t ref, void *data) {
  assert(strcmp(type, "shuso_socket") == 0);
  assert(ref != LUA_NOREF);
  luaL_unref(L, LUA_REGISTRYINDEX, ref);
  return true;
}


static bool lua_event_accept_data_wrap(lua_State *L, const char *type, void *data) {
  shuso_server_accept_data_t *accept = data;
  int top = lua_gettop(L);
  lua_checkstack(L, 3);
  
  lua_newtable(L);
  lua_event_data_socket_wrap(L, "shuso_socket", accept->socket);
  lua_setfield(L, -2, "socket");
  
  lua_rawgeti(L, LUA_REGISTRYINDEX, accept->binding->ref);
  assert(lua_istable(L, -1));
  
  lua_setfield(L, -2, "binding");
  
  assert(lua_gettop(L) == top+1);
  //TODO: shuso_server_binding_t
  return true;
}

static bool lua_event_accept_data_wrap_cleanup(lua_State *L, const char *type, void *data) {
  return true;
}

static int register_event_data_types(lua_State *L) {
  shuso_t *S = shuso_state(L);
  bool ok = true;
  ok = ok && shuso_lua_event_register_data_wrapper(S, "server_binding", &(shuso_lua_event_data_wrapper_t ){
    .wrap =           binding_data_lua_wrap,
    .unwrap =         binding_data_lua_unwrap
  });
  
  ok = ok && shuso_lua_event_register_data_wrapper(S, "shuso_socket", &(shuso_lua_event_data_wrapper_t ){
    .wrap =           lua_event_data_socket_wrap,
    .wrap_cleanup =   lua_event_data_socket_wrap_cleanup,
    .unwrap =         lua_event_data_socket_unwrap,
    .unwrap_cleanup = lua_event_data_socket_unwrap_cleanup,
  });
  
  ok = ok && shuso_lua_event_register_data_wrapper(S, "server_accept", &(shuso_lua_event_data_wrapper_t ){
    .wrap =           lua_event_accept_data_wrap,
    .wrap_cleanup =   lua_event_accept_data_wrap_cleanup,
  });
  lua_pushboolean(L, ok);
  return 1;
}

void shuttlesock_server_module_prepare(shuso_t *S, void *pd) {
  luaL_Reg lib[] = {
    {"register_event_data_types", register_event_data_types},
    {"create_binding_data", luaS_create_binding_data},
    {"start_worker_io_listener_coro", luaS_start_worker_io_listener_coroutine},
    {"stop_worker_io_listener_coro", luaS_stop_worker_io_listener_coroutine},
    {"maybe_accept_event_init", luaS_maybe_accept_event_init},
    {"accept_event_init", luaS_accept_event_init},
    {"create_shared_host_data", luaS_create_shared_host_data},
    {"handle_fd_request", luaS_handle_fd_request},
    {"free_shared_host_data", luaS_free_shared_host_data},
    {NULL, NULL}
  };
  
  luaS_register_lib(S->lua.state, "shuttlesock.modules.core.server.cfuncs", lib);
}
