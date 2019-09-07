#ifndef SHUTTLESOCK_LUA_BRIDGE_H
#define SHUTTLESOCK_LUA_BRIDGE_H

#include <shuttlesock/common.h>
#include <lua.h>

bool shuso_lua_create(shuso_t *ctx);
bool shuso_lua_initialize(shuso_t *ctx);
bool shuso_lua_destroy(shuso_t *ctx);

//lua functions here
int shuso_Lua_binding_module(lua_State *L);

#endif //SHUTTLESOCK_LUA_BRIDGE_H
