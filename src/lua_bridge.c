#include <shuttlesock.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <shuttlesock/embedded_lua_scripts.h>

bool shuso_lua_create(shuso_t *ctx) {
  if(ctx->procnum == SHUTTLESOCK_MASTER) {
    assert(ctx->lua.state == NULL);
  }
  if((ctx->lua.state = luaL_newstate()) == NULL) {
    return false;
  }
  luaL_openlibs(ctx->lua.state);
  //initialize shuttlesocky lua env
  
  return true;
}

bool shuso_lua_initialize(shuso_t *ctx) {
  lua_State *L = ctx->lua.state;
  int rc = luaL_loadbuffer(L, SHUTTLESOCK_LUA_SCRIPT_CONFIG, SHUTTLESOCK_LUA_SCRIPT_CONFIG_SIZE, "shuttlesock.config");
  if(rc != LUA_OK) {
    shuso_set_error(ctx, lua_tostring(L, -1));
    return false;
  }
  
  
  return false;
  
}

bool shuso_lua_destroy(shuso_t *ctx) {
  assert(!ctx->lua.external);
  assert(ctx->lua.state != NULL);
  lua_close(ctx->lua.state);
  ctx->lua.state = NULL;
  return true;
}
