#ifndef SHUTTLESOCK_LUA_BRIDGE_H
#define SHUTTLESOCK_LUA_BRIDGE_H

#include <shuttlesock/common.h>
#include <lua.h>

typedef struct shuso_lua_ev_watcher_s shuso_lua_ev_watcher_t;

bool shuso_lua_create(shuso_t *S);
bool shuso_lua_initialize(shuso_t *S);
bool shuso_lua_destroy(shuso_t *S);


shuso_t *shuso_state_from_lua(lua_State *L);

bool shuso_lua_set_shuttlesock_state_pointer(shuso_t *S);

//lua functions here
int shuso_Lua_shuttlesock_core_module(lua_State *L);

int shuso_Lua_do_embedded_script(lua_State *L);

//debug stuff
char *lua_dbgval(lua_State *L, int n);
void lua_printstack(lua_State *L);
void lua_mm(lua_State *L, int stack_index);

void luaS_call(lua_State *L, int nargs, int nresults);

#endif //SHUTTLESOCK_LUA_BRIDGE_H
