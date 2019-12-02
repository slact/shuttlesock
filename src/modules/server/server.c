#include <shuttlesock.h>
#include <lualib.h>
#include <lauxlib.h>
#include "server.h"

static int server_foo(lua_State *L) {
  return 0;
}


void shuttlesock_server_module_prepare(shuso_t *S, void *pd) {
  luaL_Reg lib[] = {
    {"foo", server_foo},
    {NULL, NULL}
  };
  
  luaS_register_lib(S->lua.state, "shuttlesock.modules.core.server.cfuncs", lib);
}
