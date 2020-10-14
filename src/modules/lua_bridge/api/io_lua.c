#include <shuttlesock.h>
#include <arpa/inet.h>
#include "io_lua.h"

static void lua_io_update_data_ref(lua_State *L, shuso_lua_io_data_t *data, int index) {
  if(data->ref.data != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, data->ref.data);
  }
  if(index == 0) {
    data->ref.data = LUA_NOREF;
  }
  else {
    lua_pushvalue(L, index);
    data->ref.data = luaL_ref(L, LUA_REGISTRYINDEX);
  }
}

static void lua_io_handler(shuso_t *S, shuso_io_t *io) {
  shuso_lua_io_data_t *data = io->privdata;
  lua_State           *thread = data->coroutine;
  assert(data->op_complete == false);
  
  assert(thread);
  
  if(io->error != 0) {
    lua_pushnil(thread);
    lua_pushstring(thread, strerror(io->error));
    lua_pushstring(thread, shuso_system_errnoname(io->error));
    data->num_results = 3;
    lua_io_update_data_ref(thread, data, 0);
    if(data->buf_active) {
      data->buf_active = false;
      //abandon the buffer: http://lua-users.org/lists/lua-l/2017-03/msg00257.html
    }
  }
  else {
    switch(data->op) {
      case SHUSO_IO_OP_READ:
        assert(data->buf_active);
        luaL_pushresultsize(&data->buf, io->result);
        data->num_results = 1;
        data->buf_active = false;
        break;
      
      case SHUSO_IO_OP_WRITE:
        lua_pushinteger(thread, io->result);
        data->num_results = 1;
        lua_io_update_data_ref(thread, data, 0);
        break;
        
      case SHUSO_IO_OP_ACCEPT:
        lua_pushinteger(thread, io->result_fd);
        lua_pushcfunction(thread, luaS_sockaddr_c_to_lua);
        lua_pushlightuserdata(thread, io->sockaddr);
        luaS_call(thread, 1, 1);
        lua_io_update_data_ref(thread, data, 0);
        data->num_results = 2;
        break;
      
      case SHUSO_IO_OP_CONNECT:
        lua_pushboolean(thread, io->result == 0);
        data->num_results = 1;
        break;
      
      case SHUSO_IO_OP_NONE:
        lua_pushboolean(thread, 1);
        data->num_results = 1;
        break;
        
      case SHUSO_IO_OP_CLOSE:
        lua_pushboolean(thread, 1);
        data->num_results = 1;
        break;
        
      default:
        lua_pushnil(thread);
        lua_pushfstring(thread, "unsupported SHUSO_IO_OP");
        data->num_results = 2;
        break;
    }
  }
  
  data->op_complete = true;
  data->op = SHUSO_IO_OP_NONE;
  if(lua_status(thread) == LUA_YIELD) {
    int rc = luaS_resume(thread, NULL, data->num_results, NULL);
    if(rc != LUA_YIELD) {
      //coroutine is finished -- with or without errors
      lua_State *L = io->S->lua.state;
      luaL_unref(L, LUA_REGISTRYINDEX, data->ref.coroutine);
      data->coroutine = NULL;
      //TODO: make sure this io userdata gets garbage-collected after the coro is gone
    }
  }
}


#define LUA_IO_OP_INIT(L, io, data, io_operation) \
  shuso_io_t            *io = luaL_checkudata(L, 1, "shuttlesock.core.io");  \
  shuso_lua_io_data_t   *data = io->privdata;      \
  if(L != data->coroutine) { \
    return luaL_error(L, "Lua io operation called from the wrong coroutine"); \
  } \
  if(!data->op_complete) { \
    return luaL_error(L, "Can't start new io operation while the previous one has not yet completed"); \
  } \
  assert(data->op == SHUSO_IO_OP_NONE); \
  data->op = io_operation; \
  data->op_complete = false; \
  data->num_results = 0

#define LUA_IO_OP_PARTIAL_INIT(L, io, data, partial, io_operation) \
  LUA_IO_OP_INIT(L, io, data, io_operation); \
  bool partial = data->op_partial; \
  data->op_partial = false

static int Lua_shuso_io_get_io_fd(lua_State *L) {
  shuso_io_t *io = luaL_checkudata(L, 1, "shuttlesock.core.io");
  lua_pushinteger(L, io->io_socket.fd);
  return 1;
}

static int Lua_shuso_io_get_closed(lua_State *L) {
  shuso_io_t *io = luaL_checkudata(L, 1, "shuttlesock.core.io");
  switch(io->closed) {
    case 0:
      lua_pushboolean(L, 0);
      break;
    case (SHUSO_IO_READ | SHUSO_IO_WRITE):
      lua_pushliteral(L, "rw");
      break;
    case SHUSO_IO_READ:
      lua_pushliteral(L, "r");
      break;
    case SHUSO_IO_WRITE:
      lua_pushliteral(L, "w");
      break;
    default:
      lua_pushnil(L);
  }
  return 1;
}

static int lua_io_op_read(lua_State *L) {
  LUA_IO_OP_PARTIAL_INIT(L, io, data, partial, SHUSO_IO_OP_READ);
  
  size_t len = luaL_checkinteger(L, 2);
  assert(!data->buf_active);
  data->buf_active = true;
  char *buf = luaL_buffinitsize(L, &data->buf, len);
  
  if(partial) {
    shuso_io_read_partial(io, buf, len);
  }
  else {
    shuso_io_read(io, buf, len);
  }
  return data->num_results;
}

static int lua_io_op_write(lua_State *L) {
  LUA_IO_OP_PARTIAL_INIT(L, io, data, partial, SHUSO_IO_OP_WRITE);
  const char            *str = luaL_checkstring(L, 2);
  size_t                 sz = luaL_checkinteger(L, 3);
  //WARNING: no string size checks are performed here. Do try not to shoot yourself in the foot.
  
  lua_io_update_data_ref(L, data, 2);
  
  if(partial) {
    shuso_io_write_partial(io, str, sz);
  }
  else {
    shuso_io_write(io, str, sz);
  }
  return data->num_results;
}

static int lua_io_op_accept(lua_State *L) {
  LUA_IO_OP_INIT(L, io, data, SHUSO_IO_OP_ACCEPT);
  shuso_sockaddr_t      *sockaddr;
  
  sockaddr = lua_newuserdata(L, sizeof(*sockaddr));
  lua_io_update_data_ref(L, data, -1);
  shuso_io_accept(io, sockaddr, sizeof(*sockaddr));
  return data->num_results;
}

static int lua_io_op_connect(lua_State *L) {
  LUA_IO_OP_INIT(L, io, data, SHUSO_IO_OP_CONNECT);
  shuso_io_connect(io);
  return data->num_results;
}

static int lua_io_op_close(lua_State *L) {
  LUA_IO_OP_INIT(L, io, data, SHUSO_IO_OP_CLOSE);
  shuso_io_close(io);
  return data->num_results;
}

static int lua_io_op_shutdown(lua_State *L) {
  LUA_IO_OP_INIT(L, io, data, SHUSO_IO_OP_CLOSE);
  luaL_checkstring(L, 2);
  int how = 0;
  if(luaS_streq_literal(L, 2, "rw")) {
    how = SHUT_RDWR;
  }
  else if(luaS_streq_literal(L, 2, "r")) {
    how = SHUT_RD;
  }
  else if(luaS_streq_literal(L, 2, "w")) {
    how = SHUT_WR;
  }
  
  shuso_io_shutdown(io, how);
  return data->num_results;
}

static int lua_io_op_wait(lua_State *L) {
  LUA_IO_OP_INIT(L, io, data, SHUSO_IO_OP_NONE);
  int evflags = 0;
  luaL_checkstring(L, 2);
  if(luaS_streq_literal(L, 2, "rw")) {
    evflags = SHUSO_IO_READ | SHUSO_IO_WRITE;
  }
  else if(luaS_streq_literal(L, 2, "r")) {
    evflags = SHUSO_IO_READ;
  }
  else if(luaS_streq_literal(L, 2, "w")) {
    evflags = SHUSO_IO_WRITE;
  }
  shuso_io_wait(io, evflags);
  return data->num_results;
}

static int lua_io_set_op_partial(lua_State *L) {
  shuso_io_t          *io = luaL_checkudata(L, 1, "shuttlesock.core.io");
  shuso_lua_io_data_t *data = io->privdata;
  luaL_checktype(L, 2, LUA_TBOOLEAN);
  data->op_partial = lua_toboolean(L, 2);
  lua_pushvalue(L, 1);
  return 1;
}

static int lua_io_get_op_completed(lua_State *L) {
  shuso_io_t          *io = luaL_checkudata(L, 1, "shuttlesock.core.io");
  shuso_lua_io_data_t *data = io->privdata;
  
  lua_pushboolean(L, data->op_complete);
  return 1;
}

static int lua_io_op_not_implemented(lua_State *L) {
  return luaL_error(L, "Lua io operation not implemented");
}

static int lua_io_op_sendto(lua_State *L) {
  return lua_io_op_not_implemented(L);
}
static int lua_io_op_send(lua_State *L) {
  return lua_io_op_not_implemented(L);
}
static int lua_io_op_recvfrom(lua_State *L) {
  return lua_io_op_not_implemented(L);
}
static int lua_io_op_recv(lua_State *L) {
  return lua_io_op_not_implemented(L);
}

static int Lua_shuso_io_gc(lua_State *L) {
  shuso_io_t            *io = luaL_checkudata(L, 1, "shuttlesock.core.io");
  shuso_lua_io_data_t   *d = io->privdata;
  shuso_io_abort(io);
  if(io->io_socket.fd != -1) {
    close(io->io_socket.fd);
    io->io_socket.fd = -1;
  }
  if(d->ref.name != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, d->ref.name);
    d->ref.name = LUA_NOREF;
    io->io_socket.host.name = NULL;
  }
  if(d->ref.path != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, d->ref.path);
    d->ref.path = LUA_NOREF;
  }
  if(d->ref.sockaddr != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, d->ref.sockaddr);
    d->ref.sockaddr = LUA_NOREF;
    io->io_socket.host.sockaddr = NULL;
  }
  if(d->ref.data != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, d->ref.data);
    d->ref.data = LUA_NOREF;
  }
  if(d->ref.iov != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, d->ref.iov);
    d->ref.iov = LUA_NOREF;
  }
  if(d->ref.coroutine != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, d->ref.coroutine);
    d->ref.coroutine = LUA_NOREF;
    d->coroutine = NULL;
  }
  return 0;
}

int Lua_shuso_io_create(lua_State *L) {
  struct {
    shuso_io_t          io;
    shuso_lua_io_data_t data;
  } *io_blob;
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checktype(L, 2, LUA_TTHREAD);
  
  luaL_checkstack(L, 5, NULL);
  
  io_blob = lua_newuserdata(L, sizeof(*io_blob));
  
  shuso_io_t          *io = &io_blob->io;
  shuso_lua_io_data_t *data = &io_blob->data;
  shuso_socket_t       socket;
  
  io->privdata = data;
  
  luaL_newmetatable(L, "shuttlesock.core.io");
  luaL_setfuncs(L, (luaL_Reg[]) {
    {"__gc", Lua_shuso_io_gc},
    {NULL, NULL}
  }, 0);
  //__index
  lua_newtable(L);
  luaL_setfuncs(L, (luaL_Reg[]) {
    {"get_io_fd", Lua_shuso_io_get_io_fd},
    {"get_closed",Lua_shuso_io_get_closed},
    //{"get_io_socket", Lua_shuso_io_get_socket},
    
    //operations
    {"writev",      lua_io_op_not_implemented},
    {"readv",       lua_io_op_not_implemented},
    {"read",        lua_io_op_read},
    {"write",       lua_io_op_write},
    {"sendmsg",     lua_io_op_not_implemented},
    {"sendto",      lua_io_op_sendto},
    {"send",        lua_io_op_send},
    {"recvmsg",     lua_io_op_not_implemented},
    {"recvfrom",    lua_io_op_recvfrom},
    {"recv",        lua_io_op_recv},
    {"accept",      lua_io_op_accept},
    {"connect",     lua_io_op_connect},
    {"wait",        lua_io_op_wait},
    {"close",       lua_io_op_close},
    {"shutdown",    lua_io_op_shutdown},
    
    {"op_set_partial", lua_io_set_op_partial},
    {"op_completed",   lua_io_get_op_completed},
    {NULL, NULL}
  }, 0);
  lua_setfield(L, -2, "__index");
  lua_setmetatable(L, -2);
  
  lua_getfield(L, 1, "fd");
  assert(!lua_isnil(L, -1));
  socket.fd = lua_tointeger(L, -1);
  lua_pop(L, 1);
  
  
  socket.host.type = SOCK_STREAM; //assume stream by default
  luaS_getfield_any(L, 1, 2, "udp", "UDP");
  if(lua_toboolean(L, -1)) {
    socket.host.type = SOCK_DGRAM;
  }
  lua_pop(L, 1);
  luaS_getfield_any(L, 1, 2, "tcp", "TCP");
  if(lua_toboolean(L, -1)) {
    socket.host.type = SOCK_STREAM;
  }
  lua_pop(L, 1);
  
  luaS_getfield_any(L, 1, 2, "type", "socket_type");
  if(luaS_streq_any(L, -1, 4, "tcp", "TCP", "stream", "SOCK_STREAM")) {
    socket.host.type = SOCK_STREAM;
  }
  else if(luaS_streq_any(L, -1, 4, "udp", "UDP", "dgram", "SOCK_DGRAM")) {
    socket.host.type = SOCK_DGRAM;
  }
  else if(luaS_streq_any(L, -1, 3, "raw", "SOCK_RAW")) {
    socket.host.type = SOCK_RAW;
  }
  else if(!lua_isnil(L, -1)) {
    return luaL_error(L, "invalid socket type");
  }
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "sockaddr");
  if(!lua_isnil(L, 1)) {
    socket.host.sockaddr = (void *)lua_topointer(L, -1);
    data->ref.sockaddr = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else {
    socket.host.sockaddr = NULL;
    data->ref.sockaddr = LUA_NOREF;
    lua_pop(L, 1);
  }
  
  lua_getfield(L, 1, "name");
  if(lua_isstring(L, -1)) {
    socket.host.name = lua_tostring(L, -1);
    data->ref.name = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else {
    lua_pop(L, 1);
    socket.host.name = NULL;
    data->ref.name = LUA_NOREF;
  }
  
  data->ref.path = LUA_NOREF;
  data->ref.data = LUA_NOREF;
  data->ref.iov = LUA_NOREF;
  
  if(!socket.host.sockaddr) {
    lua_pushcfunction(L, luaS_sockaddr_lua_to_c);
    lua_pushvalue(L, 1);
    luaS_call(L, 1, 2);
    if(lua_isnil(L, -2)) {
      return 2;
    }
    lua_pop(L, 1);
    socket.host.sockaddr = (void *)lua_topointer(L, -1);
    data->ref.sockaddr = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  
  lua_pushvalue(L, 2);
  data->ref.coroutine = luaL_ref(L, LUA_REGISTRYINDEX);
  data->coroutine = lua_tothread(L, 2);
  assert(data->coroutine);
    
  data->buf_active = false;
  memset(&data->buf, '\0', sizeof(data->buf));
  
  int rw = 0;
  lua_getfield(L, 1, "readwrite");
  if(lua_isboolean(L, -1)) {
    rw = lua_toboolean(L, -1) ? (SHUSO_IO_READ | SHUSO_IO_WRITE) : 0;
  }
  else if(lua_isstring(L, -1)) {
    if(luaS_streq_literal(L, -1, "rw")) {
      rw = SHUSO_IO_READ | SHUSO_IO_WRITE;
    }
    else if(luaS_streq_literal(L, -1, "r")) {
      rw = SHUSO_IO_READ;
    }
    else if(luaS_streq_literal(L, -1, "w")) {
      rw = SHUSO_IO_WRITE;
    }
    else {
      return luaL_error(L, "invalid 'readwrite' value %s", lua_tostring(L, -1));
    }
  }
  else if(lua_isnil(L, -1)) {
    return luaL_error(L, "missing 'readwrite' value");
  }
  lua_pop(L, 1);
  
  data->op = SHUSO_IO_OP_NONE;
  data->op_complete = true;
  data->op_partial = false;
  data->num_results = 0;
  
  shuso_io_init(shuso_state(L), io, &socket, rw, &lua_io_handler, data);  
  return 1;
}

