#include <shuttlesock.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
bool shuso_lua_create(shuso_t *ctx) {
  if(ctx->procnum == SHUTTLESOCK_MASTER) {
    assert(ctx->lua == NULL);
  }
  if((ctx->lua = luaL_newstate()) == NULL) {
    return false;
  }
  luaL_openlibs(ctx->lua);
  //initialize shuttlesocky lua env
  
  return true;
}

bool shuso_lua_destroy(shuso_t *ctx) {
  assert(ctx->lua != NULL);
  lua_close(ctx->lua);
  ctx->lua = NULL;
  return true;
}
