#include <shuttlesock.h>
#include <lauxlib.h>

static bool luaS_push_config_function(lua_State *L, const char *funcname) {
  lua_getglobal(L, "require");
  lua_pushliteral(L, "shuttlesock.config");
  lua_call(L, 1, 1);
  lua_getfield(L, -1, funcname);
  lua_remove(L, -2);
  return true;
}

static bool luaS_pcall_config_method(lua_State *L, const char *method_name, int nargs, bool keep_result) {
  shuso_t                    *S = shuso_state(L);
  int                         argstart = lua_absindex(L, -nargs);
  shuso_config_module_ctx_t  *ctx = S->common->module_ctx.config;
  lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->ref);
  lua_getfield(L, -1, method_name);
  lua_insert(L, argstart);
  lua_insert(L, argstart + 1);
  return luaS_function_pcall_result_ok(L, nargs + 1, keep_result);
}

shuso_setting_value_t SHUTTLESOCK_VALUES_END = {
  .type = SHUSO_VALUE_END_SENTINEL,
};

shuso_setting_t SHUTTLESOCK_SETTINGS_END = {
  .name = NULL,
};

bool shuso_config_serialize(shuso_t *S) {
  lua_State                  *L = S->lua.state;
  shuso_config_module_ctx_t  *ctx = S->common->module_ctx.config;
  if(!luaS_pcall_config_method(L, "serialize", 0, true)) {
    return false;
  }
  ctx->config_serialized_str = lua_tolstring(L, -1, &ctx->config_serialized_str_len);
  if(ctx->config_serialized_str_ref != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, ctx->config_serialized_str_ref);
  }
  ctx->config_serialized_str_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  return true;
}

bool shuso_config_unserialize(shuso_t *S) {
  lua_State                  *L = S->lua.state;
  shuso_config_module_ctx_t  *ctx = S->common->module_ctx.config;
  lua_pushlstring(L, ctx->config_serialized_str, ctx->config_serialized_str_len);
  if(!luaS_pcall_config_method(L, "unserialize", 0, true)) {
    return false;
  }
  ctx->config_serialized_str = NULL;
  ctx->config_serialized_str_len = 0;
  return true; 
}

bool shuso_config_register_setting(shuso_t *S, shuso_setting_t *setting, shuso_module_t *module) {
  lua_State *L = S->lua.state;
  lua_newtable(L);
  
  if(!setting->name) {
    return shuso_set_error(S, "module %s setting %p name cannot be NULL", module->name, setting);
  }
  
  lua_pushstring(L, setting->name);
  lua_setfield(L, -2, "name");
  
  lua_pushstring(L, setting->path);
  lua_setfield(L, -2, "path");
  
  if(!setting->nargs) {
    return shuso_set_error(S, "module %s setting %s nargs field cannot be NULL", module->name, setting->name);
  }
  lua_pushstring(L, setting->nargs);
  lua_setfield(L, -2, "nargs");
  
  lua_newtable(L);
  if(setting->default_values) {
    int i = 0;
    for(shuso_setting_value_t *val = &setting->default_values[0]; val->type != SHUSO_VALUE_END_SENTINEL; val++, i++) {
      switch(val->type) {
        case SHUSO_VALUE_END_SENTINEL:
          assert(0); //should never happen
          break;
        case SHUSO_VALUE_UNSET:
          return shuso_set_error(S, "module %s setting %s default value #%d type cannot be SHUSO_VALUE_UNSET", module->name, setting->name, i);
        case SHUSO_VALUE_STRING:
          lua_pushstring(L, val->value_string);
          break;
        case SHUSO_VALUE_INTEGER:
          lua_pushinteger(L, val->value_int);
          break;
        case SHUSO_VALUE_TIME:
          lua_pushnumber(L, val->value_time);
          break;
        case SHUSO_VALUE_FLOAT:
          lua_pushnumber(L, val->value_float);
          break;
        case SHUSO_VALUE_BOOL:
          lua_pushboolean(L, val->value_bool);
          break;
      }
      lua_rawseti(L, -2, i+1);
    }
  }
  lua_setfield(L, -2, "default");
  
  lua_pushstring(L, module->name);
  
  if(!luaS_pcall_config_method(L, "register_setting", 2, false)) {
    return false;
  }
  return true;
}

bool shuso_config_initialize(shuso_t *S) {
  lua_State *L = S->lua.state;
  shuso_config_module_ctx_t *ctx = shuso_stalloc(&S->stalloc, sizeof(*ctx));
  if(!ctx) {
    return shuso_set_error(S, "failed to allocate module context");
  }
  if(!shuso_add_module(S, &shuso_config_module)) {
    return false;
  }
  luaS_push_config_function(L, "new");
  if(!luaS_function_call_result_ok(L, 0, true)) {
    return false;
  }
  lua_reference_t ref = luaL_ref(L, LUA_REGISTRYINDEX);
  if(ref == LUA_REFNIL || ref == LUA_NOREF) {
    return shuso_set_error(S, "failed to create lua reference to new config");
  }
  *ctx =(shuso_config_module_ctx_t ) {
    .ref = ref,
    .parsed = false,
    .config_serialized_str_ref = LUA_NOREF,
    .config_serialized_str = NULL
  };
  
  return true;
}

shuso_module_t shuso_config_module = {
  .name = "config",
  .version = "0.0.1"
};
