#ifndef SHUTTLESOCK_LUA_BRIDGE_H
#define SHUTTLESOCK_LUA_BRIDGE_H

#include <shuttlesock/common.h>
#include <lua.h>

typedef struct shuso_lua_ev_watcher_s shuso_lua_ev_watcher_t;

bool shuso_lua_create(shuso_t *S);
bool shuso_lua_initialize(shuso_t *S);
bool shuso_lua_destroy(shuso_t *S);

//invoked via shuso_state(...)
shuso_t *shuso_state_from_lua(lua_State *L);

//initialization stuff
bool luaS_set_shuttlesock_state_pointer(lua_State *L, shuso_t *S);

//lua functions here
int luaS_push_core_module(lua_State *L);
int luaS_push_system_module(lua_State *L);
int luaS_do_embedded_script(lua_State *L);

//debug stuff
char *luaS_dbgval(lua_State *L, int n);
void luaS_printstack(lua_State *L);
void luaS_mm(lua_State *L, int stack_index);

//running lua functions while being nice to shuttlesock
void luaS_call(lua_State *L, int nargs, int nresults);
int luaS_resume(lua_State *thread, lua_State *from, int nargs);
int luaS_call_or_resume(lua_State *L, int nargs);
bool luaS_function_call_result_ok(lua_State *L, int nargs, bool preserve_result);
bool luaS_function_pcall_result_ok(lua_State *L, int nargs, bool preserve_result);

//system calls and such
int luaS_glob(lua_State *L); //glob(); pop 1 string, push glob table of strings or nil, err

//errorsmithing
int luaS_shuso_error(lua_State *L);

#endif //SHUTTLESOCK_LUA_BRIDGE_H
