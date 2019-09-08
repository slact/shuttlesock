#ifndef SHUTTLESOCK_LUA_BRIDGE_H
#define SHUTTLESOCK_LUA_BRIDGE_H

#include <shuttlesock/common.h>
#include <lua.h>

bool shuso_lua_create(shuso_t *ctx);
bool shuso_lua_initialize(shuso_t *ctx);
bool shuso_lua_destroy(shuso_t *ctx);


shuso_t *shuso_lua_ctx(lua_State *L);
bool shuso_lua_set_ctx(shuso_t *ctx);

//lua functions here
int shuso_Lua_shuttlesock_core_module(lua_State *L);


#endif //SHUTTLESOCK_LUA_BRIDGE_H
