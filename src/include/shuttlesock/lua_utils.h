#ifndef SHUTTLESOCK_LUA_BRIDGE_H
#define SHUTTLESOCK_LUA_BRIDGE_H

#include <shuttlesock/common.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#define SHUSO_LUA_DEBUG_ERRORS 1

extern shuso_module_t shuso_lua_bridge_module;

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
void luaS_do_embedded_script(lua_State *L, const char *name, int nargs);

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

//fails if there's an error or the function returned nil
//success on anything else, even if the function returns nothing
bool luaS_call_noerror(lua_State *L, int nargs, int nrets);

bool luaS_pcall(lua_State *L, int nargs, int nresults);

int luaS_resume(lua_State *thread, lua_State *from, int nargs, int *nret);

int luaS_coroutine_resume(lua_State *L, lua_State *coro, int nargs); //auto-xmoving lua_resume
int luaS_call_or_resume(lua_State *L, int nargs);
bool luaS_function_call_result_ok(lua_State *L, int nargs, bool preserve_result);

//copy value from one global state to another
bool luaS_gxcopy_start(lua_State *source, lua_State *destination);
bool luaS_gxcopy(lua_State *source, lua_State *destination);
bool luaS_gxcopy_module_state(lua_State *source, lua_State *destination, const char *module_name);
bool luaS_gxcopy_package_preloaders(lua_State *source, lua_State *destination);
bool luaS_gxcopy_finish(lua_State *source, lua_State *destination);

bool luaS_streq(lua_State *L, int index, const char *str);
#define luaS_streq_literal(L, index, str) \
 (lua_pushliteral(L, str) && luaS_streq(L, index < 0 ? index - 1 : index, NULL))

bool luaS_streq_any(lua_State *L, int index, int nstrings, ...);
 
void luaS_getfield_any(lua_State *L, int index, int names, ...);

int luaS_table_concat(lua_State *L, const char *delimeter); //table.concat the table at the top of the stack, popping it and pushing the concatenated string
int luaS_table_count(lua_State *L, int idx); //count all non-nil elements in table. O(n)

bool luaS_push_lua_module_field(lua_State *L, const char *module_name, const char *key); //require(module_name)[key]

bool luaS_register_lib(lua_State *L, const char *name, luaL_Reg *reg);

bool luaS_pointer_ref(lua_State *L, const char *pointer_table_name, const void *ptr);
bool luaS_pointer_unref(lua_State *L, const char *pointer_table_name, const void *ptr);
bool luaS_get_pointer_ref(lua_State *L, const char *pointer_table_name, const void *ptr);

//error handlers for lua_pcall
int luaS_traceback_error_handler(lua_State *L); //sets Lua error + traceback as the shuttlesock error message
int luaS_passthru_error_handler(lua_State *L); //just returns the error

//socket stuff
int luaS_sockaddr_lua_to_c(lua_State *L);
int luaS_sockaddr_c_to_lua(lua_State *L);
int luaS_sockaddr_family_lua_to_c(lua_State *L, int strindex);
int luaS_string_to_socktype(lua_State *L, int strindex);
void luaS_pushstring_from_socktype(lua_State *L, int socktype);

//serialize function (no upvalues!!)
int luaS_function_dump(lua_State *L);

//system calls and such
int luaS_glob(lua_State *L); //glob(); pop 1 string, push glob table of strings or nil, err

//errorsmithing
void luaS_push_shuso_error(lua_State *L);
void luaS_push_runstate(lua_State *L, shuso_runstate_t state);
#endif //SHUTTLESOCK_LUA_BRIDGE_H
