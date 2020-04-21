#include <shuttlesock.h>
#include "io_lua.h"

static void lua_io_update_data_ref(lua_State *L, shuso_lua_io_data_t *data, int index) {
  if(data->data != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, data->data);
  }
  if(index == 0) {
    data->data = LUA_NOREF;
  }
  else {
    lua_pushvalue(L, index);
    data->data = luaL_ref(L, LUA_REGISTRYINDEX);
  }
}

static void lua_io_sync_handler(shuso_t *S, shuso_io_t *io) {
  
}
static void lua_io_async_handler(shuso_t *S, shuso_io_t *io) {
  
}

static unsigned lua_io_new_op(shuso_io_t *io, shuso_lua_io_data_t *data, shuso_io_opcode_t op) {
  unsigned tick = ++data->tick;
  data->op = op;
  io->handler = lua_io_sync_handler;
  return tick;
}

static bool lua_io_op_check_finished_or_wait(shuso_io_t *io, shuso_lua_io_data_t *data, unsigned tick) {
  if(tick == data->tick && data->op == SHUSO_IO_OP_NONE) {
    return true;
  }
  data->tick++;
  io->handler = lua_io_async_handler;
  return false;
}

static int Lua_shuso_io_get_io_fd(lua_State *L) {
  shuso_io_t *io = luaL_checkudata(L, 1, "shuttlesock.core.io");
  lua_pushinteger(L, io->io_socket.fd);
  return 1;
}

static int Lua_shuso_io_get_value(lua_State *L) {
  shuso_io_t *io = luaL_checkudata(L, 1, "shuttlesock.core.io");
  if(luaS_streq_literal(L, 2, "buf")) {
    lua_pushlstring(L, io->buf, io->len);
    return 1;
  }
  if(luaS_streq_literal(L, 2, "iov")) {
    size_t iovcnt = io->iovcnt;
    lua_createtable(L, iovcnt, 0);
    for(size_t i=0; i<iovcnt; i++) {
      lua_pushlstring(L, io->iov[i].iov_base, io->iov[i].iov_len);
      lua_rawseti(L, -2, i+1);
    }
    return 1;
  }
  if(luaS_streq_literal(L, 2, "socket")) {
    return luaL_error(L, "io_get_value 'socket' not yet implemented");
  }
  if(luaS_streq_literal(L, 2, "hostinfo")) {
    return luaL_error(L, "io_get_value 'hostinfo' not yet implemented");
  }
  if(luaS_streq_literal(L, 2, "sockaddr")) {
    return luaL_error(L, "io_get_value 'sockaddr' not yet implemented");
  }
  if(luaS_streq_literal(L, 2, "result")) {
    lua_pushinteger(L, io->result);
    return 1;
  }
  if(luaS_streq_literal(L, 2, "result_intdata")) {
    lua_pushinteger(L, io->result_intdata);
    return 1;
  }
  if(luaS_streq_literal(L, 2, "result_fd")) {
    lua_pushinteger(L, io->result_fd);
    return 1;
  }
  if(luaS_streq_literal(L, 2, "error")) {
    if(io->error == 0) {
      //no error
      lua_pushboolean(L, 0);
      return 1;
    }
    else {
      lua_pushinteger(L, io->error);
      lua_pushstring(L, strerror(io->error));
      return 2;
    }
  }
  return luaL_error(L, "io_get_value '%s' not valid or implemented", lua_tostring(L, 2));
}


static int Lua_shuso_io_set_value(lua_State *L) {
  shuso_io_t          *io = luaL_checkudata(L, 1, "shuttlesock.core.io");
  shuso_lua_io_data_t *data = io->privdata;
  
  luaL_checkstack(L, 4, NULL);
  if(luaS_streq_literal(L, 2, "buf")) {
    io->buf = (char *)luaL_checklstring(L, 3, &io->len);
    lua_io_update_data_ref(L, data, 3);
  }
  else if(luaS_streq_literal(L, 2, "iov")) {
    luaL_checktype(L, 3, LUA_TTABLE);
    size_t   iovcnt = luaL_len(L, 3);
    struct iovec *iov;
    
    lua_createtable(L, 2, 0); //reftable for iovec userdata and string table
    
    iov = lua_newuserdata(L, sizeof(iov) * iovcnt);
    for(size_t i=0; i<iovcnt; i++) {
      lua_rawgeti(L, 3, i+1);
      iov[i].iov_base = (char *)lua_tolstring(L, -1, &iov[i].iov_len);
      lua_pop(L, 1);
    }
    lua_rawseti(L, -2, 1);
    
    lua_pushvalue(L, 3);
    lua_rawseti(L, -2, 1);
    
    lua_io_update_data_ref(L, data, -1);
    lua_pop(L, 1);
  }
  else {
    return luaL_error(L, "io_set_value '%s' not valid or unimplemented", lua_tostring(L, 2));
  }
  
  lua_pushboolean(L, 1);
  return 1;
}
static void lua_io_op_read(lua_State *L, shuso_io_t *io, int narg, bool partial) {
  shuso_lua_io_data_t *data = io->privdata;
  size_t len = luaL_checkinteger(L, narg);
  assert(!data->buf_active);
  data->buf_active = true;
  char *buf = luaL_buffinitsize(L, &data->buf, len);
  int tick = lua_io_new_op(io, data, SHUSO_IO_OP_READ);
  data->op = SHUSO_IO_OP_READ;
  if(partial) {
    shuso_io_read_partial(io, buf, len);
  }
  else {
    shuso_io_read(io, buf, len);
  }
  if(lua_io_op_check_finished_or_wait(io, data, tick)) {
    //TODO: return result
  }
}
static void lua_io_op_write(lua_State *L, shuso_io_t *io, int narg, bool partial) {
  shuso_lua_io_data_t *data = io->privdata;
  size_t               sz;
  const char          *str = luaL_checklstring(L, 3, &sz);
  
  lua_io_update_data_ref(L, data, 3);
  int tick = lua_io_new_op(io, data, SHUSO_IO_OP_WRITE);
  if(partial) {
    shuso_io_write_partial(io, str, sz);
  }
  else {
    shuso_io_write(io, str, sz);
  }
  if(lua_io_op_check_finished_or_wait(io, data, tick)) {
    //TODO: return result
  }
}

static void lua_io_op_accept(lua_State *L, shuso_io_t *io) {
  shuso_lua_io_data_t *data = io->privdata;
  int tick = lua_io_new_op(io, data, SHUSO_IO_OP_ACCEPT);
  shuso_io_accept(io);
  if(lua_io_op_check_finished_or_wait(io, data, tick)) {
    //TODO: return result
  }
}

static void lua_io_op_connect(lua_State *L, shuso_io_t *io) {
  
}
static void lua_io_op_wait(lua_State *L, shuso_io_t *io, int narg) {
  
}

static int Lua_shuso_io_op(lua_State *L) {
  shuso_io_t          *io = luaL_checkudata(L, 1, "shuttlesock.core.io");
  
  luaL_checkstring(L, 2);
  if(luaS_streq_literal(L, 2, "writev")) {
    return luaL_error(L, "not implemented");
  }
  else if(luaS_streq_literal(L, 2, "readv")) {
    return luaL_error(L, "not implemented");
  }
  
  else if(luaS_streq_literal(L, 2, "writev_partial")) {
    return luaL_error(L, "not implemented");
  }
  else if(luaS_streq_literal(L, 2, "readv_partial")) {
    return luaL_error(L, "not implemented");
  }
  
  else if(luaS_streq_literal(L, 2, "write")) {
    lua_io_op_write(L, io, 3, false);
  }
  else if(luaS_streq_literal(L, 2, "read")) {
    lua_io_op_read(L, io, 3, false);
  }
  
  else if(luaS_streq_literal(L, 2, "write_partial")) {
    lua_io_op_write(L, io, 3, true);
  }
  else if(luaS_streq_literal(L, 2, "read_partial")) {
    lua_io_op_read(L, io, 3, true);
  }
  else if(luaS_streq_literal(L, 2, "connect")) {
    lua_io_op_connect(L, io);
  }
  else if(luaS_streq_literal(L, 2, "accept")) {
    lua_io_op_accept(L, io);
  }
  else if(luaS_streq_literal(L, 2, "wait")) {
    lua_io_op_wait(L, io, 3);
  }
  lua_pushboolean(L, 1);
  return 1;
}
static int Lua_shuso_io_resume(lua_State *L) {
  return 0;
}



static int Lua_shuso_io_gc(lua_State *L) {
  shuso_io_t            *io = luaL_checkudata(L, 1, "shuttlesock.core.io");
  shuso_lua_io_data_t   *d = io->privdata;
  
  if(io->io_socket.fd != -1) {
    close(io->io_socket.fd);
    io->io_socket.fd = -1;
  }
  if(d->self_waiting != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, d->self_waiting);
    d->self_waiting = LUA_NOREF;
  }
  if(d->name != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, d->name);
    d->name = LUA_NOREF;
    io->io_socket.host.name = NULL;
  }
  if(d->path != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, d->path);
    d->path = LUA_NOREF;
    io->io_socket.host.path = NULL;
  }
  if(d->sockaddr != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, d->sockaddr);
    d->sockaddr = LUA_NOREF;
    io->io_socket.host.sockaddr = NULL;
  }
  if(d->data != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, d->data);
    d->data = LUA_NOREF;
  }
  if(d->iov != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, d->iov);
    d->iov = LUA_NOREF;
  }
  if(d->handler != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, d->handler);
    d->handler = LUA_NOREF;
  }
  return 0;
}

int Lua_shuso_io_create(lua_State *L) {
  struct {
    shuso_io_t          io;
    shuso_lua_io_data_t data;
  } *io_blob;
  
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  
  luaL_checkstack(L, 5, NULL);
  
  io_blob = lua_newuserdata(L, sizeof(*io_blob));
  
  shuso_io_t          *io = &io_blob->io;
  shuso_lua_io_data_t *data = &io_blob->data;
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
    //{"get_io_socket", Lua_shuso_io_get_socket},
    {"get_value", Lua_shuso_io_get_value},
    {"set_value", Lua_shuso_io_set_value},
    {"op", Lua_shuso_io_op},
    {"resume", Lua_shuso_io_resume},
    {NULL, NULL}
  }, 0);
  lua_setfield(L, -2, "__index");
  lua_setmetatable(L, -2);
  
  lua_getfield(L, 1, "fd");
  assert(!lua_isnil(L, -1));
  io->io_socket.fd = lua_tointeger(L, 1);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "name");
  if(lua_isstring(L, -1)) {
    io->io_socket.host.name = lua_tostring(L, -1);
    data->name = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else {
    lua_pop(L, 1);
    data->name = LUA_NOREF;
  }
  luaS_mm(L, 1);
  lua_getfield(L, 1, "addr_family");
  if(luaS_streq_literal(L, -1, "AF_INET")) {
    io->io_socket.host.addr_family = AF_INET;
    
    lua_getfield(L, 1, "addr_binary");
    if(!lua_isstring(L, -1)) {
      lua_pushnil(L);
      lua_pushliteral(L, "missing addr_binary field");
      return 2;
    }
    size_t sz;
    const char *addr = lua_tolstring(L, -1, &sz);
    assert(sz == sizeof(io->io_socket.host.addr));
    memcpy(&io->io_socket.host.addr, addr, sz);
    lua_pop(L, 1);
  }
  else if(luaS_streq_literal(L, -1, "AF_INET6")) {
#ifdef SHUTTLESOCK_HAVE_IPV6
    io->io_socket.host.addr_family = AF_INET6;
    size_t sz;
    lua_getfield(L, 1, "addr_binary");
    if(!lua_isstring(L, -1)) {
      lua_pushnil(L);
      lua_pushliteral(L, "missing addr_binary field");
      return 2;
    }
    const char *addr6 = lua_tolstring(L, -1, &sz);
    assert(sz == sizeof(io->io_socket.host.addr6));
    memcpy(&io->io_socket.host.addr6, (void *)addr6, sz);
    lua_pop(L, 1);
#else
    lua_pushnil(L);
    lua_pushliteral(L, "Can't create IPv6 io coro: IPv6 is not supported on this system");
    return 2;
#endif
  }
  else if(luaS_streq_literal(L, -1, "AF_UNIX")) {
    io->io_socket.host.addr_family = AF_UNIX;
  }
  else {
    lua_pushnil(L);
    lua_pushfstring(L, "invalid addr_family: %s", lua_isstring(L, -1) ? lua_tostring(L, -1) : "<?>");
    return 2;
  }
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "path");
  if(lua_isstring(L, -1)) {
    io->io_socket.host.path = lua_tostring(L, -1);
    data->path = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else {
    data->path = LUA_NOREF;
    lua_pop(L, 1);
  }
  
  lua_getfield(L, 1, "sockaddr");
  if(!lua_isnil(L, 1)) {
    io->io_socket.host.sockaddr = (void *)lua_topointer(L, -1);
    data->sockaddr = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else {
    data->sockaddr = LUA_NOREF;
    lua_pop(L, 1);
  }
  
  lua_getfield(L, 1, "udp");
  io->io_socket.host.udp = lua_toboolean(L, -1);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "port");
  io->io_socket.host.port = lua_tointeger(L, -1);
  lua_pop(L, 1);
  
  lua_pushvalue(L, 2);
  data->handler = luaL_ref(L, LUA_REGISTRYINDEX);
  data->data = LUA_NOREF;
  
  data->self_waiting = LUA_NOREF;
  
  data->iov = LUA_NOREF;
  
  data->buf_active = false;
  memset(&data->buf, '\0', sizeof(data->buf));
  
  return 1;
}

