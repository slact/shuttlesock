#include <shuttlesock.h>
#include "api/lua_ipc.h"
#include "lua_bridge.h"

static void lua_module_gxcopy_loaded_packages(shuso_t *S, shuso_event_state_t *es, intptr_t code, void *data, void *pd) {
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

static void lua_module_stop_process_event(shuso_t *S, shuso_event_state_t *es, intptr_t code, void *data, void *pd) {
  lua_State *L = S->lua.state;
  shuso_lua_bridge_module_ctx_t *ctx = shuso_core_context(S, &shuso_lua_bridge_module);
  if(ctx->ipc_messages_active > 0 && shuso_event_delay(S, es, "Lua IPC messages still in transit", 0.050, NULL)) {
    return;
  }
  
  luaS_push_lua_module_field(L, "shuttlesock.ipc", "shutdown_from_shuttlesock_core");
  luaS_call(L, 0, 0);
}

static bool lua_bridge_module_initialize(shuso_t *S, shuso_module_t *self) {
  shuso_event_listen(S, "core:worker.start.before.lua_gxcopy", lua_module_gxcopy_loaded_packages, self);
  shuso_event_listen(S, "core:worker.stop", lua_module_stop_process_event, self);
  shuso_event_listen(S, "core:manager.stop", lua_module_stop_process_event, self);
  shuso_event_listen(S, "core:master.stop", lua_module_stop_process_event, self);
  
  
  shuso_lua_bridge_module_ctx_t *ctx = shuso_stalloc(&S->stalloc, sizeof(*ctx));
  *ctx = (shuso_lua_bridge_module_ctx_t ) {
    .ipc_messages_active = 0
  };
  
  shuso_set_core_context(S, self, ctx);
  
  return true;
}

shuso_module_t shuso_lua_bridge_module = {
  .name = "lua_bridge",
  .version = SHUTTLESOCK_VERSION_STRING,
  .subscribe = 
   " core:worker.start.before.lua_gxcopy"
   " core:worker.stop"
   " core:manager.stop"
   " core:master.stop",
  .initialize = lua_bridge_module_initialize
};
