#include <shuttlesock.h>
#include <shuttlesock/modules/lua_bridge.h>

static void lua_module_gxcopy(shuso_t *S, shuso_event_state_t *es, intptr_t code, void *data, void *pd) {
  shuso_t *Sm = data;
  lua_State *L = S->lua.state;
  lua_State *Lm = Sm->lua.state;
  
  //copy over all required modules that have a metatable and __gxcopy
  lua_getglobal(Lm, "package");
  lua_getfield(Lm, -1, "loaded");
  lua_remove(Lm, -2);
  lua_pushnil(Lm);  /* first key */
  while(lua_next(Lm, -2) != 0) {
    if(!lua_getmetatable(Lm, -1)) {
      lua_pop(Lm, 1);
      continue;
    }
    lua_getfield(Lm, -1, "__gxcopy_save_module_state");
    if(lua_isnil(Lm, -1)) {
      lua_pop(Lm, 3);
      continue;
    }
    lua_pop(Lm, 3);
    
    luaS_gxcopy_module_state(Lm, L, lua_tostring(Lm, -1));
  }
  lua_pop(Lm, 1);
}

static bool lua_bridge_module_initialize(shuso_t *S, shuso_module_t *self) {
  shuso_event_listen(S, "core:worker.start.before.lua_gxcopy", lua_module_gxcopy, self);
  
  shuso_lua_bridge_module_ctx_t *ctx = shuso_stalloc(&S->stalloc, sizeof(*ctx));
  
  shuso_set_core_context(S, self, ctx);
  
  return true;
}

shuso_module_t shuso_lua_bridge_module = {
  .name = "lua_bridge",
  .version = "0.0.1",
  .subscribe = 
   " core:worker.start.before.lua_gxcopy",
  .initialize = lua_bridge_module_initialize
};
