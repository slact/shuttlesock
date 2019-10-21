#ifndef SHUTTLESOCK_LUA_BRIDGE_H
#define SHUTTLESOCK_LUA_BRIDGE_H

#include <shuttlesock/common.h>
#include <lua.h>

extern shuso_module_t shuso_lua_bridge_module;

typedef struct shuso_lua_ev_watcher_s shuso_lua_ev_watcher_t;

bool shuso_lua_create(shuso_t *S);
bool shuso_lua_initialize(shuso_t *S);
bool shuso_lua_destroy(shuso_t *S);

//invoked via shuso_state(...)
shuso_t *shuso_state_from_lua(lua_State *L);

//initialization stuff
bool luaS_set_shuttlesock_state_pointer(lua_State *L, shuso_t *S);
bool shuso_register_lua_event_data_types(shuso_t *S);

//lua functions here
int luaS_push_core_module(lua_State *L);
int luaS_push_system_module(lua_State *L);
int luaS_do_embedded_script(lua_State *L);

//debug stuff
char *luaS_dbgval(lua_State *L, int n);
void luaS_mm(lua_State *L, int stack_index);
void luaS_inspect(lua_State *L, int stack_index);
void luaS_push_inspect_string(lua_State *L, int stack_index);

/* macro wizardry to have variadic luaS_printstack() call */
void luaS_printstack_named(lua_State *L, const char*);
#define ___LUAS_PRINTSTACK_VARARG(_1,_2,NAME,...) NAME
#define luaS_printstack(...) ___LUAS_PRINTSTACK_VARARG(__VA_ARGS__, LUAS_PRINTSTACK_2, LUAS_PRINTSTACK_1, ___END__VARARG__LIST__)(__VA_ARGS__)
#define LUAS_PRINTSTACK_2(L, name) luaS_printstack_named(L, name)
#define LUAS_PRINTSTACK_1(L) luaS_printstack_named(L, "")


//running lua functions while being nice to shuttlesock
void luaS_call(lua_State *L, int nargs, int nresults);
bool luaS_pcall(lua_State *L, int nargs, int nresults);
int luaS_resume(lua_State *thread, lua_State *from, int nargs);
int luaS_call_or_resume(lua_State *L, int nargs);
bool luaS_function_call_result_ok(lua_State *L, int nargs, bool preserve_result);
bool luaS_function_pcall_result_ok(lua_State *L, int nargs, bool preserve_result);

//copy value from one global state to another
bool luaS_gxcopy_start(lua_State *source, lua_State *destination);
bool luaS_gxcopy(lua_State *source, lua_State *destination);
bool luaS_gxcopy_module_state(lua_State *source, lua_State *destination, const char *module_name);
bool luaS_gxcopy_finish(lua_State *source, lua_State *destination);


bool luaS_streq(lua_State *L, int index, const char *str);
#define luaS_streq_literal(L, index, str) \
 (lua_pushliteral(L, str) && luaS_streq(L, index, NULL))

int luaS_table_concat(lua_State *L, const char *delimeter); //table.concat the table at the top of the stack, popping it and pushing the concatenated string
int luaS_table_count(lua_State *L, int idx); //count all non-nil elements in table. O(n)

bool luaS_push_lua_module_field(lua_State *L, const char *module_name, const char *key); //require(module_name)[key]

#define luaS_pointer_ref(L, pointer_table_name, ptr) do { \
  lua_getfield(L, LUA_REGISTRYINDEX, pointer_table_name); \
  if(lua_isnil(L, -1)) { \
    lua_pop(L, 1); \
    lua_newtable(L); \
    lua_pushvalue(L, -1); \
    lua_setfield(L, LUA_REGISTRYINDEX, pointer_table_name); \
  } \
  lua_pushlightuserdata(L, (void *)ptr); \
  lua_pushvalue(L, -3); \
  lua_settable(L, -3); \
  lua_pop(L, 2); \
} while(0)

#define luaS_pointer_unref(L, pointer_table_name, ptr) do { \
  lua_getfield(L, LUA_REGISTRYINDEX, pointer_table_name); \
  if(!lua_isnil(L, -1)) { \
    lua_pushlightuserdata(L, (void *)ptr); \
    lua_pushnil(L); \
    lua_settable(L, -3); \
  } \
  lua_pop(L, 1); \
} while(0)

#define luaS_get_pointer_ref(L, pointer_table_name, ptr) do { \
  lua_getfield(L, LUA_REGISTRYINDEX, pointer_table_name); \
  if(!lua_istable(L, -1)) { \
    lua_pop(L, 1); \
    lua_pushnil(L); \
  } \
  else { \
    lua_pushlightuserdata(L, (void *)ptr); \
    lua_gettable(L, -2); \
    lua_remove(L, -2); \
  } \
} while(0)

//serialize function (no upvalues!!)
int luaS_function_dump(lua_State *L);

//system calls and such
int luaS_glob(lua_State *L); //glob(); pop 1 string, push glob table of strings or nil, err

//errorsmithing
int luaS_shuso_error(lua_State *L);

void luaS_push_runstate(lua_State *L, shuso_runstate_t state);
#endif //SHUTTLESOCK_LUA_BRIDGE_H
