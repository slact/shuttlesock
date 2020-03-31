#include <shuttlesock.h>
#include "api/ipc_lua_api.h"
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


 static bool lua_event_data_basic_wrap(lua_State *L, const char *type, void *data) {
  if(type == NULL || data == NULL) {
    lua_pushnil(L);
    return true;
  }
  
  lua_pushstring(L, type);
  if(luaS_streq_literal(L, -1, "string")) {
    lua_pop(L, 1);
    lua_pushstring(L, (char *)data);
    return true;
  }
  else if(luaS_streq_literal(L, -1, "float")) {
    lua_pop(L, 1);
    lua_pushnumber(L, *(double *)data);
    return true;
  }
  else if(luaS_streq_literal(L, -1, "integer")) {
    lua_pop(L, 1);
    lua_pushinteger(L, *(int *)data);
    return true;
  }
  else {
    shuso_set_error(shuso_state(L), "don't know how to wrap event data type '%s'", type);
    lua_pop(L, 1);
    lua_pushnil(L);
    return false;
  }
}
static bool lua_event_data_basic_wrap_cleanup(lua_State *L, const char *type, void *data) {
  return true;
}
static lua_reference_t lua_event_data_basic_unwrap(lua_State *L, const char *type, int narg, void **ret) {
  if(type  == NULL) {
    *ret = NULL;
    return LUA_NOREF;
  }
  
  lua_pushstring(L, type);
  if(luaS_streq_literal(L, -1, "string")) {
    lua_pop(L, 1);
    *(const char **)ret = lua_tostring(L, narg);
    lua_pushvalue(L, narg);
    return luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else if(luaS_streq_literal(L, -1, "float")) {
    lua_pop(L, 1);
    double *dubs = lua_newuserdata(L, sizeof(double));
    *dubs = lua_tonumber(L, narg);
    *ret = dubs;
    return luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else if(luaS_streq_literal(L, -1, "integer")) {
    int *integer = lua_newuserdata(L, sizeof(int));
    *integer = lua_tointeger(L, narg);
    *ret = integer;
    return luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else {
    *ret = NULL;
    shuso_set_error(shuso_state(L), "don't know how to unwrap event data type '%s'", type);
    return LUA_NOREF;
  }
}

static bool lua_event_data_basic_unwrap_cleanup(lua_State *L, const char *datatype, lua_reference_t ref, void *data) {
  if(ref == LUA_REFNIL || ref == LUA_NOREF) {
    //nothing to clean up
    return true;
  }
  luaL_unref(L, LUA_REGISTRYINDEX, ref);
  return true;
}

bool shuso_lua_event_register_data_wrapper(shuso_t *S, const char *name, shuso_lua_event_data_wrapper_t *wrapper) {
  lua_State *L = S->lua.state;
  int top = lua_gettop(L);
  luaS_push_lua_module_field(L, "shuttlesock.core", "event_data_wrappers");
  assert(lua_istable(L, -1));
  
  shuso_lua_event_data_wrapper_t *stored_wrapper = shuso_stalloc(&S->stalloc, sizeof(*stored_wrapper));
  if(!stored_wrapper) {
    lua_settop(L, top);
    return false;
  }
  *stored_wrapper = *wrapper;
  
  lua_pushlightuserdata(L, stored_wrapper);
  lua_setfield(L, -2, name);
  lua_settop(L, top);
  return true;
}

static bool lua_bridge_module_initialize(shuso_t *S, shuso_module_t *self) {
  shuso_event_listen(S, "core:worker.start.before.lua_gxcopy", lua_module_gxcopy_loaded_packages, self);
  shuso_event_listen(S, "core:worker.stop", lua_module_stop_process_event, self);
  shuso_event_listen(S, "core:manager.stop", lua_module_stop_process_event, self);
  shuso_event_listen(S, "core:master.stop", lua_module_stop_process_event, self);
  
  shuso_lua_event_data_wrapper_t basic_wrapper = {
    .wrap =           lua_event_data_basic_wrap,
    .wrap_cleanup =   lua_event_data_basic_wrap_cleanup,
    .unwrap =         lua_event_data_basic_unwrap,
    .unwrap_cleanup = lua_event_data_basic_unwrap_cleanup,
  };
  
  shuso_lua_event_register_data_wrapper(S, "string", &basic_wrapper);
  shuso_lua_event_register_data_wrapper(S, "float", &basic_wrapper);
  shuso_lua_event_register_data_wrapper(S, "integer", &basic_wrapper);
  
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
