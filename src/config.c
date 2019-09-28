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
  ctx->serialized.str = lua_tolstring(L, -1, &ctx->serialized.len);
  if(ctx->serialized.ref != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, ctx->serialized.ref);
  }
  ctx->serialized.ref = luaL_ref(L, LUA_REGISTRYINDEX);
  return true;
}

bool shuso_config_unserialize(shuso_t *S) {
  lua_State                  *L = S->lua.state;
  shuso_config_module_ctx_t  *ctx = S->common->module_ctx.config;
  lua_pushlstring(L, ctx->serialized.str, ctx->serialized.len);
  if(!luaS_pcall_config_method(L, "unserialize", 0, true)) {
    return false;
  }
  ctx->serialized.str = NULL;
  ctx->serialized.len = 0;
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

bool shuso_config_system_initialize(shuso_t *S) {
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
    .serialized.ref = LUA_NOREF,
    .serialized.str = NULL,
    .serialized.len = 0
  };
  
  S->common->module_ctx.config = ctx;
  return true;
}

bool shuso_config_system_generate(shuso_t *S) {
  lua_State *L = S->lua.state;
  if(!luaS_pcall_config_method(L, "all_settings", 0, true)) {
    return false;
  }
  
  luaS_printstack(L);
  
  //create C structs from config settings
  lua_pushnil(L);
  while(lua_next(L, -2)) {
    shuso_setting_t *setting = shuso_stalloc(&S->stalloc, sizeof(*setting));
    if(!setting) {
      return shuso_set_error(S, "failed to alloc config setting");
    }
    
    lua_getfield(L, -1, "name");
    setting->name = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    setting->aliases = NULL;
    setting->path = NULL;
    setting->description = NULL;
    
    lua_getfield(L, -1, "values");
    int values_count = luaL_len(L, -1);
    setting->values = shuso_stalloc(&S->stalloc, sizeof(*setting->values) + sizeof(shuso_setting_value_t) * values_count);
    setting->values->count = values_count;
    for(int i = 1; i<= setting->values->count; i++) {
      lua_rawgeti(L, -1, i);
      shuso_setting_value_t *val = &setting->values->array[i-1];
      switch(lua_type(L, -1)) {
        case LUA_TNUMBER:
          if(lua_isinteger(L, -1)) {
            val->type = SHUSO_VALUE_INTEGER;
            val->value_int = lua_tointeger(L, -1);
            val->len = sizeof(val->value_int);
          }
          else {
            val->type = SHUSO_VALUE_FLOAT;
            val->value_float = lua_tonumber(L, -1);
            val->len = sizeof(val->value_float);
          }
          break;
        case LUA_TSTRING:
          val->type = SHUSO_VALUE_STRING;
          val->value_string = lua_tolstring(L, -1, &val->len);
          break;
        case LUA_TBOOLEAN:
          val->type = SHUSO_VALUE_BOOL;
          val->value_bool = lua_toboolean(L, -1);
          val->len = sizeof(val->value_bool);
          break;
        case LUA_TNIL:
        case LUA_TTABLE:
        case LUA_TFUNCTION:
        case LUA_TUSERDATA:
        default:
          assert(0); // not supposed to happen
          return false;
      }
      lua_pop(L, 1);
    }
    
    lua_getfield(L, -1, "block");
    if(lua_isnil(L, -1)) {
      lua_pop(L, 1);
    }
    else {
      shuso_setting_block_t *block = shuso_stalloc(&S->stalloc, sizeof(*block));
      block->name = setting->name;
      lua_getfield(L, -1, "ptr");
      assert(lua_isnil(L, -1));
      lua_pop(L, 1);
      lua_pushlightuserdata(L, block);
      lua_setfield(L, -2, "ptr");
      
      block->ref = luaL_ref(L, LUA_REGISTRYINDEX); //also pops the setting-block table
    }
    lua_pop(L, 1);
  }
  
  
  for(int i = 1; i<= setting->values->count; i++) {
    lua_rawgeti(L, -1, i);
    lua_getfield(L, -1, "inherit_from_setting"
  }
  
  luaS_printstack(L);
  
  //walk the blocks again, and let the modules set their settings for each block
  if(!luaS_pcall_config_method(L, "all_blocks", 0, true)) {
    return false;
  }
  lua_pushnil(L);
  while(lua_next(L, -2)) {
    shuso_setting_block_t      *block;
    lua_getfield(L, -1, "ptr");
    block = (void *)lua_topointer(L, -1);
    lua_pop(L, 1);
    assert(block != NULL);
    
    
  }
  
  return true;
}

bool shuso_config_file_parse(shuso_t *S, const char *config_file_path) {
  return false;
}
bool shuso_config_string_parse(shuso_t *S, const char *config) {
  return false;
}

shuso_module_t shuso_config_module = {
  .name = "config",
  .version = "0.0.1"
};
