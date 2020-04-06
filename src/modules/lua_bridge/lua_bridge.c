#include <shuttlesock.h>
#include "api/ipc_lua_api.h"
#include "lua_bridge.h"
#include <arpa/inet.h>

static void lua_module_gxcopy_loaded_packages(shuso_t *S, shuso_event_state_t *es, intptr_t code, void *data, void *pd) {
  shuso_t *Sm = data;
  lua_State *L = S->lua.state;
  lua_State *Lm = Sm->lua.state;
  
  //copy over all required modules that have a metatable and __gxcopy
  lua_getglobal(Lm, "package");
  lua_getfield(Lm, -1, "loaded");
  lua_remove(Lm, -2);
  lua_pushnil(Lm);  /* first key */
  while(lua_next(Lm, -2) != 0) {
    if(!lua_getmetatable(Lm, -1)) {
      lua_pop(Lm, 1);
      continue;
    }
    lua_getfield(Lm, -1, "__gxcopy_save_module_state");
    if(lua_isnil(Lm, -1)) {
      lua_pop(Lm, 3);
      continue;
    }
    lua_pop(Lm, 3);
    
    luaS_gxcopy_module_state(Lm, L, lua_tostring(Lm, -1));
  }
  lua_pop(Lm, 1);
}

static void lua_module_stop_process_event(shuso_t *S, shuso_event_state_t *es, intptr_t code, void *data, void *pd) {
  lua_State *L = S->lua.state;
  shuso_lua_bridge_module_ctx_t *ctx = shuso_core_context(S, &shuso_lua_bridge_module);
  if(ctx->ipc_messages_active > 0 && shuso_event_delay(S, es, "Lua IPC messages still in transit", 0.050, NULL)) {
    return;
  }
  
  luaS_push_lua_module_field(L, "shuttlesock.ipc", "shutdown_from_shuttlesock_core");
  luaS_call(L, 0, 0);
}


 static bool lua_event_data_basic_wrap(lua_State *L, const char *type, void *data) {
  if(type == NULL || data == NULL) {
    lua_pushnil(L);
    return true;
  }
  
  lua_pushstring(L, type);
  if(luaS_streq_literal(L, -1, "string")) {
    lua_pop(L, 1);
    lua_pushstring(L, (char *)data);
    return true;
  }
  else if(luaS_streq_literal(L, -1, "float")) {
    lua_pop(L, 1);
    lua_pushnumber(L, *(double *)data);
    return true;
  }
  else if(luaS_streq_literal(L, -1, "integer")) {
    lua_pop(L, 1);
    lua_pushinteger(L, *(int *)data);
    return true;
  }
  else {
    shuso_set_error(shuso_state(L), "don't know how to wrap event data type '%s'", type);
    lua_pop(L, 1);
    lua_pushnil(L);
    return false;
  }
}
static bool lua_event_data_basic_wrap_cleanup(lua_State *L, const char *type, void *data) {
  return true;
}
static lua_reference_t lua_event_data_basic_unwrap(lua_State *L, const char *type, int idx, void **ret) {
  if(type  == NULL) {
    *ret = NULL;
    return LUA_NOREF;
  }
  
  lua_pushstring(L, type);
  if(luaS_streq_literal(L, -1, "string")) {
    lua_pop(L, 1);
    *(const char **)ret = lua_tostring(L, idx);
    lua_pushvalue(L, idx);
    return luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else if(luaS_streq_literal(L, -1, "float")) {
    lua_pop(L, 1);
    double *dubs = lua_newuserdata(L, sizeof(double));
    *dubs = lua_tonumber(L, idx);
    *ret = dubs;
    return luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else if(luaS_streq_literal(L, -1, "integer")) {
    int *integer = lua_newuserdata(L, sizeof(int));
    *integer = lua_tointeger(L, idx);
    *ret = integer;
    return luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else {
    *ret = NULL;
    shuso_set_error(shuso_state(L), "don't know how to unwrap event data type '%s'", type);
    return LUA_NOREF;
  }
}

static bool lua_event_data_basic_unwrap_cleanup(lua_State *L, const char *datatype, lua_reference_t ref, void *data) {
  if(ref == LUA_REFNIL || ref == LUA_NOREF) {
    //nothing to clean up
    return true;
  }
  luaL_unref(L, LUA_REGISTRYINDEX, ref);
  return true;
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



bool shuso_lua_event_register_data_wrapper(shuso_t *S, const char *name, shuso_lua_event_data_wrapper_t *wrapper) {
  lua_State *L = S->lua.state;
  int top = lua_gettop(L);
  luaS_push_lua_module_field(L, "shuttlesock.core", "event_data_wrappers");
  assert(lua_istable(L, -1));
  
  shuso_lua_event_data_wrapper_t *stored_wrapper = shuso_stalloc(&S->stalloc, sizeof(*stored_wrapper));
  if(!stored_wrapper) {
    lua_settop(L, top);
    return false;
  }
  *stored_wrapper = *wrapper;
  
  lua_pushlightuserdata(L, stored_wrapper);
  lua_setfield(L, -2, name);
  lua_settop(L, top);
  return true;
}

static bool lua_bridge_module_initialize(shuso_t *S, shuso_module_t *self) {
  shuso_event_listen(S, "core:worker.start.before.lua_gxcopy", lua_module_gxcopy_loaded_packages, self);
  shuso_event_listen(S, "core:worker.stop", lua_module_stop_process_event, self);
  shuso_event_listen(S, "core:manager.stop", lua_module_stop_process_event, self);
  shuso_event_listen(S, "core:master.stop", lua_module_stop_process_event, self);
  
  shuso_lua_event_data_wrapper_t basic_wrapper = {
    .wrap =           lua_event_data_basic_wrap,
    .wrap_cleanup =   lua_event_data_basic_wrap_cleanup,
    .unwrap =         lua_event_data_basic_unwrap,
    .unwrap_cleanup = lua_event_data_basic_unwrap_cleanup,
  };
  
  shuso_lua_event_register_data_wrapper(S, "string", &basic_wrapper);
  shuso_lua_event_register_data_wrapper(S, "float", &basic_wrapper);
  shuso_lua_event_register_data_wrapper(S, "integer", &basic_wrapper);
  
  shuso_lua_event_register_data_wrapper(S, "shuso_socket", &(shuso_lua_event_data_wrapper_t ){
    .wrap =           lua_event_data_socket_wrap,
    .wrap_cleanup =   lua_event_data_socket_wrap_cleanup,
    .unwrap =         lua_event_data_socket_unwrap,
    .unwrap_cleanup = lua_event_data_socket_unwrap_cleanup,
  });
  
  shuso_lua_event_register_data_wrapper(S, "shuso_server_accept_data", &(shuso_lua_event_data_wrapper_t ){
    .wrap =           lua_event_accept_data_wrap,
    .wrap_cleanup =   lua_event_accept_data_wrap_cleanup,
  });
  
  shuso_lua_bridge_module_ctx_t *ctx = shuso_stalloc(&S->stalloc, sizeof(*ctx));
  *ctx = (shuso_lua_bridge_module_ctx_t ) {
    .ipc_messages_active = 0
  };
  
  shuso_set_core_context(S, self, ctx);
  
  return true;
}

shuso_module_t shuso_lua_bridge_module = {
  .name = "lua_bridge",
  .version = SHUTTLESOCK_VERSION_STRING,
  .subscribe = 
   " core:worker.start.before.lua_gxcopy"
   " core:worker.stop"
   " core:manager.stop"
   " core:master.stop",
  .initialize = lua_bridge_module_initialize
};
