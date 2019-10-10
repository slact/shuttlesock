#ifndef SHUTTLESOCK_LUA_MODULE_H
#define SHUTTLESOCK_LUA_MODULE_H
#include <lua.h>
#include <lauxlib.h>
#include <shuttlesock/common.h>

extern shuso_module_t shuso_lua_module;
bool shuso_add_lua_module(shuso_t *S, int pos);

#endif //SHUTTLESOCK_LUA_MODULE_H
