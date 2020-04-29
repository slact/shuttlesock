#include <shuttlesock.h>
#include <lualib.h>
#include <lauxlib.h>
#include "server.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <shuttlesock/modules/config/private.h>

static int luaS_create_binding_data(lua_State *L) {
  shuso_t                  *S = shuso_state(L);
  shuso_server_binding_t   *binding = shuso_stalloc(&S->stalloc, sizeof(*binding));
  if(!binding) {
    return luaL_error(L, "failed to allocate memory for server binding");
  }
  binding->lua_hostnum = lua_tointeger(L, 2);
  lua_getfield(L, 1, "server_type");
  const char *server_type = lua_tostring(L, -1);
  binding->server_type = shuso_stalloc(&S->stalloc, strlen(server_type)+1);
  if(!binding->server_type) {
    return luaL_error(L, "failed to allocate memory for server binding server_type string");
  }
  strcpy((char *)binding->server_type, server_type);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "name");
  const char *name = lua_tostring(L, -1);
  binding->host.name = shuso_stalloc(&S->stalloc, strlen(name)+1);
  if(!binding->host.name) {
    return luaL_error(L, "failed to allocate memory for server binding server_type string");
  }
  strcpy((char *)binding->host.name, name);
  lua_pop(L, 1); //pop ["name"]
    
  binding->host.sockaddr = shuso_stalloc(&S->stalloc, sizeof(*binding->host.sockaddr));
  if(!binding->host.sockaddr) {
    return luaL_error(L,"couldn't allocate host sockaddr");
  }
  
  lua_pushcfunction(L, luaS_sockaddr_lua_to_c);
  lua_getfield(L, 1, "address");
  
  lua_getfield(L, -1, "type");
  binding->host.type = luaS_string_to_socktype(L, -1);
  assert(binding->host.type != 0);
  lua_pop(L, 1);
  
  lua_pushlightuserdata(L, binding->host.sockaddr);
  luaS_call(L, 2, 2);
  if(lua_isnil(L, -2)) {
    return luaL_error(L, "%s", lua_tostring(L, -1));
  }
  assert(lua_topointer(L, -2) == binding->host.sockaddr);
  lua_pop(L, 2);
  
  binding->host.family = binding->host.sockaddr->any.sa_family;
  
  lua_getfield(L, 1, "common_parent_block");
  assert(lua_istable(L, -1));
  lua_getfield(L, -1, "ptr");
  assert(lua_islightuserdata(L, -1) || lua_isuserdata(L, -1));
  binding->config.common_parent_block = (void *)lua_topointer(L, -1);
  lua_pop(L, 2);
  assert(binding->config.common_parent_block);
  
  lua_getfield(L, 1, "listen");
  int listen_count = luaL_len(L, -1);
  binding->config.count = listen_count;
  binding->config.array = shuso_stalloc(&S->stalloc, sizeof(*binding->config.array) * listen_count);
  if(!binding->config.array) {
    return luaL_error(L, "couldn't allocate server host config array");
  }
  
  for(int j=0; j<listen_count; j++) {
    
    lua_geti(L, -1, j+1);
    
    lua_getfield(L, -1, "block");
    
    lua_getfield(L, -1, "ptr");
    binding->config.array[j].block = (void *)lua_topointer(L, -1);
    lua_pop(L, 2); //pop .block.ptr
    
    lua_getfield(L, -1, "setting");
    lua_getfield(L, -1, "ptr");
    binding->config.array[j].setting = (void *)lua_topointer(L, -1);
    lua_pop(L, 2); //pop .setting.ptr
    
    lua_pop(L, 1); //pop listen[i]
  }
  
  lua_pop(L, 1); //pop ["listen"]
  
  lua_pushlightuserdata(L, binding);
  return 1;
}

static int luaS_create_binding_table_from_ptr(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  const shuso_server_binding_t *binding = lua_topointer(L, 1);
  
  lua_newtable(L);
  
  lua_pushstring(L, binding->server_type);
  lua_setfield(L, -2, "type");
  
  if(binding->host.name) {
    lua_pushstring(L, binding->host.name);
    lua_setfield(L, -2, "name");
  }
  
  lua_pushcfunction(L, luaS_sockaddr_c_to_lua);
  lua_pushlightuserdata(L, binding->host.sockaddr);
  luaS_call(L, 1, 2);
  if(lua_isnil(L, -2)) {
    return 2;
  }
  else {
    lua_setfield(L, -2, "address");
  }
  
  lua_newtable(L);
  for(unsigned i=0; i<binding->config.count; i++) {
    lua_newtable(L);
    lua_pushstring(L, binding->server_type);
    lua_setfield(L, -2, "type");
    
    luaS_push_lua_module_field(L, "shuttlesock.config", "setting");
    lua_pushlightuserdata(L, binding->config.array[i].setting);
    luaS_call(L, 1, 1);
    assert(lua_istable(L, -1));
    lua_setfield(L, -2, "setting");
    
    luaS_push_lua_module_field(L, "shuttlesock.config", "block");
    lua_pushlightuserdata(L, binding->config.array[i].block);
    luaS_call(L, 1, 1);
    assert(lua_istable(L, -1));
    
    lua_setfield(L, -2, "block");
    lua_rawseti(L, -2, i+1);
  }
  lua_setfield(L, -2, "listen");
  
  if(binding->config.common_parent_block) {
    luaS_push_lua_module_field(L, "shuttlesock.config", "block");
    lua_pushlightuserdata(L, binding->config.common_parent_block);
    luaS_call(L, 1, 1);
    lua_setfield(L, -2, "common_parent_block");
  }
  
  return 1;
}

typedef struct {
  shuso_io_t                io;
  shuso_event_t            *maybe_accept_event;
  shuso_event_t            *accept_event;
  shuso_server_binding_t   *binding;
  shuso_sockaddr_t          sockaddr;
  socklen_t                 sockaddr_len;
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
    raise(SIGABRT);
    shuso_set_error_errno(S, "failed to listen on %s: %s", d->binding->host.name ? d->binding->host.name : "(?)", strerror(errno));
  }
  else {
    shuso_log_info(S, "Listening on %s", d->binding->host.name ? d->binding->host.name : "(?)");
  }
  while(rc == 0) {
    SHUSO_IO_CORO_YIELD(wait, SHUSO_IO_READ);
    SHUSO_IO_CORO_YIELD(accept, &d->sockaddr, sizeof(d->sockaddr));
    maybe_accept_data.sockaddr = &d->sockaddr;
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
  
  //recreate sockaddr
  assert(host->sockaddr);
  sa_family_t fam = host->sockaddr->any.sa_family;
  switch(fam) {
    case AF_INET:
      shared_host->sockaddr = shuso_shared_slab_alloc(&S->common->shm, sizeof(struct sockaddr_in));
      if(!shared_host->sockaddr) {
        goto fail;
      }
      shared_host->sockaddr->in = host->sockaddr->in;
      break;
#ifdef SHUTTLESOCK_HAVE_IPV6
    case AF_INET6:
      shared_host->sockaddr = shuso_shared_slab_alloc(&S->common->shm, sizeof(struct sockaddr_in6));
      if(!shared_host->sockaddr) {
        goto fail;
      }
      shared_host->sockaddr->in6 = host->sockaddr->in6;
      break;
#endif
    case AF_UNIX:
      shared_host->sockaddr = shuso_shared_slab_alloc(&S->common->shm, sizeof(struct sockaddr_un));
      if(!shared_host->sockaddr) {
        goto fail;
      }
      shared_host->sockaddr->un = host->sockaddr->un;
      break;
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
  int fd = socket(hostinfo->family, hostinfo->type, 0);
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
  
  assert(hostinfo->sockaddr);
  
  if(bind(fd, &hostinfo->sockaddr->any, shuso_sockaddr_len(hostinfo->sockaddr)) == -1) {
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
  lua_checkstack(L, 3);
  luaS_push_lua_module_field(L, "shuttlesock.modules.core.server", "get_binding");
  lua_pushlightuserdata(L, binding);
  luaS_call(L, 1, 1);
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

  assert(data->sockaddr->any.sa_family == data->binding->host.family);
  socket.host.sockaddr = data->sockaddr;
  socket.host.family = data->binding->host.family;
  socket.host.type = data->binding->host.type;
  
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
  int tindex = lua_gettop(L);
  
  lua_pushinteger(L, sock->fd);
  lua_setfield(L, tindex, "fd");
  
  if(sock->host.name) {
    lua_pushstring(L, sock->host.name);
    lua_setfield(L, tindex, "name");
  }
  
  switch(sock->host.type) {
    case SOCK_STREAM:
      lua_pushliteral(L, "stream");
      break;
    case SOCK_DGRAM:
      lua_pushliteral(L, "dgram");
    case SOCK_RAW:
      lua_pushliteral(L, "raw");
  }
  lua_setfield(L, tindex, "type");
  
  lua_pushcfunction(L, luaS_sockaddr_c_to_lua);
  lua_pushlightuserdata(L, sock->host.sockaddr);
  lua_pushvalue(L, tindex);
  luaS_call(L, 2, 0);
  
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
  
  shuso_socket_t *sock = lua_newuserdata(L, sizeof(*sock));
  
  lua_getfield(L, idx, "fd");
  sock->fd = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : -1;
  lua_pop(L, 1);
  
  lua_getfield(L, idx, "name");
  sock->host.name = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
  lua_pop(L, 1);
  
  lua_getfield(L, idx, "type");
  sock->host.type = luaS_string_to_socktype(L, -1);
  lua_pop(L, 1);
  
  lua_pushcfunction(L, luaS_sockaddr_lua_to_c);
  lua_pushvalue(L, idx);
  luaS_call(L, 1, 1);
  lua_setuservalue(L, -2);
  
  lua_reference_t ref = luaL_ref(L, LUA_REGISTRYINDEX);
  
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
  
  luaS_push_lua_module_field(L, "shuttlesock.modules.core.server", "get_binding");
  lua_pushlightuserdata(L, accept->binding);
  luaS_call(L, 1, 1);
  assert(lua_istable(L, -1));
  
  lua_setfield(L, -2, "binding");
  
  assert(lua_gettop(L) == top+1);
  //TODO: shuso_server_binding_t
  return true;
}

static bool lua_event_accept_data_wrap_cleanup(lua_State *L, const char *type, void *data) {
  return true;
}

static bool lua_event_maybe_accept_data_wrap(lua_State *L, const char *type, void *evdata) {
  shuso_server_tentative_accept_data_t *data = evdata;
  int top = lua_gettop(L);
  lua_checkstack(L, 3);
  
  lua_newtable(L);
  
  lua_pushinteger(L, data->fd);
  lua_setfield(L, -2, "fd");
  
  if(data->accept_event) {
    lua_pushstring(L, data->accept_event->name);
    lua_setfield(L, -2, "accept_event_name");
    
    lua_pushlightuserdata(L, data->accept_event);
    lua_setfield(L, -2, "accept_event_ptr");
  }
  
  switch(data->sockaddr->any.sa_family) {
    case AF_INET: {
      lua_pushliteral(L, "IPv4");
      lua_setfield(L, -2, "family");
      
      lua_pushlstring(L, (char *)&data->sockaddr->in.sin_addr, sizeof(data->sockaddr->in.sin_addr));
      lua_setfield(L, -2, "address_binary");
      
      char  address_str[INET_ADDRSTRLEN];
      if(inet_ntop(AF_INET, (char *)&data->sockaddr->in.sin_addr, address_str, INET_ADDRSTRLEN)) {
        lua_pushstring(L, address_str);
        lua_setfield(L, -2, "address");
      }
      
      lua_pushinteger(L, ntohs(data->sockaddr->in.sin_port));
      lua_setfield(L, -2, "port");
      
      break;
    }
#ifdef SHUTTLESOCK_HAVE_IPV6
    case AF_INET6: {
      lua_pushliteral(L, "IPv6");
      lua_setfield(L, -2, "family");
      
      lua_pushlstring(L, (char *)&data->sockaddr->in6.sin6_addr, sizeof(data->sockaddr->in6.sin6_addr));
      lua_setfield(L, -2, "address_binary");
      char address_str[INET6_ADDRSTRLEN];
      if(inet_ntop(AF_INET6, (char *)&data->sockaddr->in6.sin6_addr, address_str, INET6_ADDRSTRLEN)) {
        lua_pushstring(L, address_str);
        lua_setfield(L, -2, "address");
      }
      
      lua_pushinteger(L, ntohs(data->sockaddr->in6.sin6_port));
      lua_setfield(L, -2, "port");
      
      break;
    }
#endif
    case AF_UNIX:
      lua_pushliteral(L, "unix");
      lua_setfield(L, -2, "family");
      
      lua_pushstring(L, data->sockaddr->un.sun_path);
      lua_setfield(L, -2, "path");
      break;
  }
  
  luaS_push_lua_module_field(L, "shuttlesock.modules.core.server", "get_binding");
  lua_pushlightuserdata(L, data->binding);
  luaS_call(L, 1, 1);
  assert(lua_istable(L, -1));
  
  lua_setfield(L, -2, "binding");
  
  assert(lua_gettop(L) == top+1);
  //TODO: shuso_server_binding_t
  return true;
}

static bool lua_event_maybe_accept_data_wrap_cleanup(lua_State *L, const char *type, void *data) {
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
  
  ok = ok && shuso_lua_event_register_data_wrapper(S, "server_maybe_accept", &(shuso_lua_event_data_wrapper_t ){
    .wrap =           lua_event_maybe_accept_data_wrap,
    .wrap_cleanup =   lua_event_maybe_accept_data_wrap_cleanup,
  });
    
  lua_pushboolean(L, ok);
  return 1;
}

void shuttlesock_server_module_prepare(shuso_t *S, void *pd) {
  luaL_Reg lib[] = {
    {"register_event_data_types", register_event_data_types},
    {"create_binding_data", luaS_create_binding_data},
    {"create_binding_table_from_ptr", luaS_create_binding_table_from_ptr},
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
