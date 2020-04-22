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
  printf("YEah hey?...\n");
  assert(data->op_complete == false);
  
  assert(thread);
  
  if(io->error != 0) {
    lua_pushnil(thread);
    lua_pushstring(thread, strerror(io->error));
    data->num_results = 2;
    luaS_printstack(thread, "yep");
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
        
      case SHUSO_IO_OP_ACCEPT: {
        lua_pushinteger(thread, io->result_fd);
        
        lua_newtable(thread);
        switch(io->sockaddr.any.sa_family) {
          case AF_INET: {          
            lua_pushlstring(thread, (char *)&io->sockaddr.inet.sin_addr, sizeof(io->sockaddr.inet.sin_addr));
            lua_setfield(thread, -2, "addr_binary");
            
            char str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &io->sockaddr, str, INET_ADDRSTRLEN);
            lua_pushstring(thread, str);
            lua_setfield(thread, -2, "address");
            
            lua_pushliteral(thread, "IPv4");
            lua_setfield(thread, -2, "family");
            
            lua_pushinteger(thread, ntohs(io->sockaddr.inet.sin_port));
            lua_setfield(thread, -2, "port");
            break;
          }
          case AF_INET6: {
            lua_pushlstring(thread, (char *)&io->sockaddr.inet6.sin6_addr, sizeof(io->sockaddr.inet6.sin6_addr));
            lua_setfield(thread, -2, "addr_binary");
            
            char str[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &io->sockaddr, str, INET6_ADDRSTRLEN);
            lua_pushstring(thread, str);
            lua_setfield(thread, -2, "address");
            
            lua_pushliteral(thread, "IPv6");
            lua_setfield(thread, -2, "family");
            
            lua_pushinteger(thread, ntohs(io->sockaddr.inet6.sin6_port));
            lua_setfield(thread, -2, "port");
            break;
          }
          
          case AF_UNIX:
            raise(SIGABRT);
            //TODO
            break;
        }
        data->num_results = 2;
        break;
      }
      
      case SHUSO_IO_OP_CONNECT:
        lua_pushboolean(thread, io->result == 0);
        data->num_results = 1;
        break;
      
      case SHUSO_IO_OP_NONE:
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
    int rc = luaS_resume(thread, NULL, data->num_results);
    if(rc != LUA_YIELD) {
      //coroutine is finished -- with or without errors
      lua_State *L = io->S->lua.state;
      luaL_unref(L, LUA_REGISTRYINDEX, data->ref.coroutine);
      data->coroutine = NULL;
      //TODO: make sure this io userdata gets garbage-collected after the coro is gone
    }
  }
  luaS_printstack(thread, "after_handler");
}

static void lua_io_new_op(shuso_io_t *io, shuso_lua_io_data_t *data, shuso_io_opcode_t op) {
  data->op = op;
  assert(data->op_complete);
  data->op_complete = false;
  data->num_results = 0;
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
static void lua_io_op_read(lua_State *L, shuso_io_t *io, int index_sz, bool partial) {
  shuso_lua_io_data_t *data = io->privdata;
  lua_State           *thread = data->coroutine;
  size_t len = luaL_checkinteger(L, index_sz);
  assert(thread);
  assert(!data->buf_active);
  data->buf_active = true;
  char *buf = luaL_buffinitsize(thread, &data->buf, len);
  lua_io_new_op(io, data, SHUSO_IO_OP_READ);
  data->op = SHUSO_IO_OP_READ;
  if(partial) {
    shuso_io_read_partial(io, buf, len);
  }
  else {
    shuso_io_read(io, buf, len);
  }
}
static void lua_io_op_write(lua_State *L, shuso_io_t *io, int index_str, int index_sz, bool partial) {
  shuso_lua_io_data_t *data = io->privdata;
  const char          *str = luaL_checkstring(L, index_str);
  size_t               sz = luaL_checkinteger(L, index_sz);
  
  lua_io_update_data_ref(L, data, index_str);
  lua_io_new_op(io, data, SHUSO_IO_OP_WRITE);
  if(partial) {
    shuso_io_write_partial(io, str, sz);
  }
  else {
    shuso_io_write(io, str, sz);
  }
}

static void lua_io_op_accept(lua_State *L, shuso_io_t *io) {
  shuso_lua_io_data_t *data = io->privdata;
  lua_io_new_op(io, data, SHUSO_IO_OP_ACCEPT);
  shuso_io_accept(io);
}

static void lua_io_op_connect(lua_State *L, shuso_io_t *io) {
  shuso_lua_io_data_t *data = io->privdata;
  lua_io_new_op(io, data, SHUSO_IO_OP_CONNECT);
  shuso_io_connect(io);
}
static void lua_io_op_wait(lua_State *L, shuso_io_t *io, int index_wait_type) {
  shuso_lua_io_data_t *data = io->privdata;
  int evflags = 0;
  if(luaS_streq_literal(L, index_wait_type, "rw")) {
    evflags = SHUSO_IO_READ | SHUSO_IO_WRITE;
  }
  else if(luaS_streq_literal(L, index_wait_type, "r")) {
    evflags = SHUSO_IO_READ;
  }
  else if(luaS_streq_literal(L, index_wait_type, "w")) {
    evflags = SHUSO_IO_WRITE;
  }
  lua_io_new_op(io, data, SHUSO_IO_OP_NONE);
  shuso_io_wait(io, evflags);
}

static int Lua_shuso_io_op(lua_State *L) {
  shuso_io_t          *io = luaL_checkudata(L, 1, "shuttlesock.core.io");
  shuso_lua_io_data_t *data = io->privdata;
  
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
    lua_io_op_write(L, io, 3, 4, false);
  }
  else if(luaS_streq_literal(L, 2, "read")) {
    lua_io_op_read(L, io, 3, false);
  }
  
  else if(luaS_streq_literal(L, 2, "write_partial")) {
    lua_io_op_write(L, io, 3, 4, true);
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
  
  return data->num_results;
}
static int Lua_shuso_io_resume(lua_State *L) {
  return 0;
}

static int Lua_shuso_io_op_completed(lua_State *L) {
  shuso_io_t          *io = luaL_checkudata(L, 1, "shuttlesock.core.io");
  shuso_lua_io_data_t *data = io->privdata;
  
  lua_pushboolean(L, data->op_complete);
  return 1;
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
    io->io_socket.host.path = NULL;
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
    //{"get_io_socket", Lua_shuso_io_get_socket},
    {"get_value", Lua_shuso_io_get_value},
    {"set_value", Lua_shuso_io_set_value},
    {"op", Lua_shuso_io_op},
    {"op_completed", Lua_shuso_io_op_completed},
    {"resume", Lua_shuso_io_resume},
    {NULL, NULL}
  }, 0);
  lua_setfield(L, -2, "__index");
  lua_setmetatable(L, -2);
  
  lua_getfield(L, 1, "fd");
  assert(!lua_isnil(L, -1));
  socket.fd = lua_tointeger(L, -1);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "udp");
  socket.host.udp = lua_toboolean(L, -1);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "port");
  socket.host.port = lua_tointeger(L, -1);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "path");
  if(lua_isstring(L, -1)) {
    socket.host.path = lua_tostring(L, -1);
    data->ref.path = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else {
    socket.host.path = NULL;
    data->ref.path = LUA_NOREF;
    lua_pop(L, 1);
  }
  
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
  luaS_mm(L, 1);
  lua_getfield(L, 1, "addr_family");
  if(luaS_streq_literal(L, -1, "AF_INET")) {
    socket.host.addr_family = AF_INET;
    
    lua_getfield(L, 1, "addr_binary");
    if(!lua_isstring(L, -1)) {
      lua_pushnil(L);
      lua_pushliteral(L, "missing addr_binary field");
      return 2;
    }
    size_t sz;
    const char *addr = lua_tolstring(L, -1, &sz);
    assert(sz == sizeof(socket.host.addr));
    memcpy(&socket.host.addr, addr, sz);
    lua_pop(L, 1);
    
    if(socket.host.sockaddr == NULL) {
      struct sockaddr_in  *sa_in = lua_newuserdata(L, sizeof(*sa_in));
      sa_in->sin_family = AF_INET;
      sa_in->sin_port = htons(socket.host.port);
      sa_in->sin_addr = socket.host.addr;
      data->ref.sockaddr = luaL_ref(L, LUA_REGISTRYINDEX);
      socket.host.sockaddr_in = sa_in;
    }
  }
  else if(luaS_streq_literal(L, -1, "AF_INET6")) {
#ifdef SHUTTLESOCK_HAVE_IPV6
    socket.host.addr_family = AF_INET6;
    size_t sz;
    lua_getfield(L, 1, "addr_binary");
    if(!lua_isstring(L, -1)) {
      lua_pushnil(L);
      lua_pushliteral(L, "missing addr_binary field");
      return 2;
    }
    const char *addr6 = lua_tolstring(L, -1, &sz);
    assert(sz == sizeof(socket.host.addr6));
    memcpy(&socket.host.addr6, (void *)addr6, sz);
    lua_pop(L, 1);
    
    if(socket.host.sockaddr == NULL) {
      struct sockaddr_in6  *sa_in6 = lua_newuserdata(L, sizeof(*sa_in6));
      sa_in6->sin6_family = AF_INET6;
      sa_in6->sin6_port = htons(socket.host.port);
      sa_in6->sin6_addr = socket.host.addr6;
      data->ref.sockaddr = luaL_ref(L, LUA_REGISTRYINDEX);
      socket.host.sockaddr_in6 = sa_in6;
    }
#else
    lua_pushnil(L);
    lua_pushliteral(L, "Can't create IPv6 io coro: IPv6 is not supported on this system");
    return 2;
#endif
  }
  else if(luaS_streq_literal(L, -1, "AF_UNIX")) {
    socket.host.addr_family = AF_UNIX;
    
    if(socket.host.sockaddr == NULL) {
      struct sockaddr_un  *sa_un = lua_newuserdata(L, sizeof(*sa_un));
      sa_un->sun_family = AF_UNIX;
      size_t sz = strlen(socket.host.path) + 1;
      if(sz > sizeof(sa_un->sun_path)) {
        sz = sizeof(sa_un->sun_path);
      }
      memcpy(sa_un->sun_path, socket.host.path, sz);
      data->ref.sockaddr = luaL_ref(L, LUA_REGISTRYINDEX);
    }
  }
  else {
    lua_pushnil(L);
    lua_pushfstring(L, "invalid addr_family: %s", lua_isstring(L, -1) ? lua_tostring(L, -1) : "<?>");
    return 2;
  }
  lua_pop(L, 1);
  
  lua_pushvalue(L, 2);
  data->ref.coroutine = luaL_ref(L, LUA_REGISTRYINDEX);
  data->coroutine = lua_tothread(L, 2);
  assert(data->coroutine);
  
  data->ref.data = LUA_NOREF;
  
  data->ref.iov = LUA_NOREF;
  
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
  data->num_results = 0;
  
  shuso_io_init(shuso_state(L), io, &socket, rw, &lua_io_handler, data);  
  return 1;
}

