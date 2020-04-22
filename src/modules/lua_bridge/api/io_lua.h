#ifndef SHUTTLESOCK_LUA_BRIDGE_IO_LUA_H
#define SHUTTLESOCK_LUA_BRIDGE_IO_LUA_H

#include <shuttlesock/common.h>
#include <shuttlesock/io.h>

typedef struct {
  lua_reference_t       self_waiting;
  lua_reference_t       name;
  lua_reference_t       path;
  lua_reference_t       sockaddr;
  lua_reference_t       data;
  lua_reference_t       iov;
  lua_reference_t       handler;
  shuso_io_opcode_t     op;
  unsigned              tick;
  luaL_Buffer           buf;
  bool                  buf_active;
} shuso_lua_io_data_t;

int Lua_shuso_io_create(lua_State *L);
  
#endif //SHUTTLESOCK_LUA_BRIDGE_IO_LUA_H