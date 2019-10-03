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

shuso_module_setting_t SHUTTLESOCK_SETTINGS_END = {
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

bool shuso_config_register_setting(shuso_t *S, shuso_module_setting_t *setting, shuso_module_t *module) {
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
  
  if(setting->description) {
    lua_pushstring(L, setting->description);
    lua_setfield(L, -2, "description");
  }
  
  if(setting->default_value) {
    lua_pushstring(L, setting->default_value);
    lua_setfield(L, -2, "default");
  }
  
  //register_setting parameters
  
  lua_pushstring(L, module->name);
  lua_insert(L, -2);
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

static shuso_setting_values_t  *lua_setting_values_to_c_struct(lua_State *L, shuso_stalloc_t *st) {
  shuso_t                 *S = shuso_state(L);
  int                      values_count = luaL_len(L, -1);
  shuso_setting_values_t  *v = shuso_stalloc(&S->stalloc, sizeof(*v) + sizeof(shuso_setting_value_t) * values_count);
  if(!v) {
    shuso_set_error(S, "failed to allocate setting values array");
    return NULL;
  }
  v->count = values_count;
  for(int i = 1; i <= values_count; i++) {
    lua_rawgeti(L, -1, i);
    shuso_setting_value_t *val = &v->array[i-1];
    
    lua_getfield(L, -1, "string");
    if(lua_isstring(L, -1)) {
      val->valid.string = true;
      val->string = lua_tolstring(L, -1, &val->string_len);
    }
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "number");
    if(lua_isnumber(L, -1)) {
      val->valid.number = true;
      val->number = lua_tonumber(L, -1);
    }
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "integer");
    if(lua_isinteger(L, -1)) {
      val->valid.integer = true;
      val->integer = lua_tointeger(L, -1);
    }
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "boolean");
    if(lua_isboolean(L, -1)) {
      val->valid.boolean = true;
      val->boolean = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
    
    lua_pop(L, 1);
  }
  return v;
}

bool shuso_config_system_generate(shuso_t *S) {
  lua_State *L = S->lua.state;
  
  if(!luaS_pcall_config_method(L, "handle", 0, false)) {
    return false;
  }
  
  //create C structs for root
  if(!luaS_pcall_config_method(L, "get_root", 0, true)) {
    return false;
  }
  
  shuso_setting_block_t *root_block = shuso_stalloc(&S->stalloc, sizeof(*root_block));
  if(root_block == NULL) {
    return shuso_set_error(S, "failed to allocate memory for config root block");
  }
  lua_getfield(L, -1, "block");
  *root_block = (shuso_setting_block_t ) {
    .name = "::ROOT"
  };
  lua_pushlightuserdata(L, root_block);
  lua_setfield(L, -2, "ptr");
  root_block->ref = luaL_ref(L, LUA_REGISTRYINDEX); //also pops the block table
  
  shuso_setting_t *root_setting = shuso_stalloc(&S->stalloc, sizeof(*root_setting));
  if(!root_setting) {
    return shuso_set_error(S, "failed to allocate memory for config root");
  }
  *root_setting = (shuso_setting_t ) {
    .name = "::ROOT",
    .values = {
      .local = NULL,
      .inherited = NULL,
      .defaults = NULL,
      .merged = NULL
    },
    .block = root_block
  };
  lua_pushlightuserdata(L, root_setting);
  lua_setfield(L, -2, "ptr");
  root_setting->ref = luaL_ref(L, LUA_REGISTRYINDEX); //also pops the setting-block table
  root_block->setting = root_setting;
  //finished setting up root structs
  
  
  if(!luaS_pcall_config_method(L, "all_settings", 0, true)) {
    return false;
  }
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
    
    lua_getfield(L, -1, "values");
    setting->values.local = lua_setting_values_to_c_struct(L, &S->stalloc);
    if(!setting->values.local) {
      return false;
    }
    lua_pop(L, 1);
    
    lua_pushvalue(L, -1);
    if(!luaS_pcall_config_method(L, "default_values", 1, true)) {
      return false;
    }
    setting->values.defaults = lua_setting_values_to_c_struct(L, &S->stalloc);
    if(!setting->values.defaults) {
      return false;
    }
    lua_pop(L, 1);
    
    lua_pushvalue(L, -1);
    if(!luaS_pcall_config_method(L, "inherited_values", 1, true)) {
      return false;
    }
    setting->values.inherited = lua_setting_values_to_c_struct(L, &S->stalloc);
    if(!setting->values.inherited) {
      return false;
    }
    lua_pop(L, 1);
    
    if(setting->values.local) {
      setting->values.merged = setting->values.local;
    }
    else if(setting->values.inherited) {
      setting->values.merged = setting->values.inherited;
    }
    else if(setting->values.defaults) {
      setting->values.merged = setting->values.defaults;
    }
    else {
      return shuso_set_error(S, "couldn't merge setting %s values", setting->name);
    }
    
    lua_getfield(L, -1, "block");
    if(lua_isnil(L, -1)) {
      lua_pop(L, 1);
      setting->block = NULL;
    }
    else {
      shuso_setting_block_t *block = shuso_stalloc(&S->stalloc, sizeof(*block));
      if(block == NULL) {
        return shuso_set_error(S, "failed to allocate memory for config block");
      }
      block->name = setting->name;
      lua_getfield(L, -1, "ptr");
      assert(lua_isnil(L, -1));
      lua_pop(L, 1);
      lua_pushlightuserdata(L, block);
      lua_setfield(L, -2, "ptr");
      
      block->ref = luaL_ref(L, LUA_REGISTRYINDEX); //also pops the setting-block table
      setting->block = block;
      block->setting = setting;
    }
    
    setting->ref = luaL_ref(L, LUA_REGISTRYINDEX); //also pops the setting table
  }
  
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
    
    for(unsigned i=0; i< S->common->modules.count; i++) {
      shuso_module_t *module = S->common->modules.array[i];
      
      lua_pushvalue(L, -1);
      lua_pushstring(L, module->name);
      if(!luaS_pcall_config_method(L, "block_handled_by_module", 2, true)) {
        return false;
      }
      if(lua_toboolean(L, -1)) {
        if(!module->initialize_config) {
          return shuso_set_error(S, "module %s initialize_config is required, but is set to NULL", module->name);
        }
        
        
        module->initialize_config(S, module, block);
        
        
      }
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  
  shuso_config_module_ctx_t  *ctx = S->common->module_ctx.config;
  assert(ctx->blocks.root == NULL);
  
  if(!luaS_pcall_config_method(L, "get_root", 0, true)) {
    return false;
  }
  lua_getfield(L, -1, "ptr");
  ctx->blocks.root = lua_topointer(L, -1);
  assert(ctx->blocks.root);
  lua_pop(L, 1);
  
  if(!luaS_pcall_config_method(L, "all_blocks", 0, true)) {
    return false;
  }
  ctx->blocks.count = luaL_len(L, -1);
  ctx->blocks.array = shuso_stalloc(&S->stalloc, sizeof(*ctx->blocks.array) * ctx->blocks.count);
  if(ctx->blocks.array == NULL) {
    return shuso_set_error(S, "failed to allocate config block array");
  }
  lua_pushnil(L);
  while(lua_next(L, -2)) {
    int i = lua_tointeger(L, -2) + 1;
    lua_getfield(L, -1, "ptr");
    ctx->blocks.array[i] = lua_topointer(L, -1);
    lua_pop(L, 2);
  }
  lua_pop(L, 1);
  
  return true;
}

bool shuso_config_file_parse(shuso_t *S, const char *config_file_path) {
  return false;
}
bool shuso_config_string_parse(shuso_t *S, const char *config) {
  lua_State *L = S->lua.state;
  lua_pushstring(L, config);
  if(!luaS_pcall_config_method(L, "parse", 1, true)) {
    return false;
  }
  return true;
}

shuso_setting_t *shuso_setting(shuso_t *S, const shuso_setting_block_t *block, const char *name) {
  lua_State         *L = S->lua.state;
  shuso_setting_t   *setting;
  lua_pushstring(L, name);
  lua_rawgeti(L, LUA_REGISTRYINDEX, block->setting->ref);
  if(!luaS_pcall_config_method(L, "find_setting", 2, true)) {
    return NULL;
  }
  lua_getfield(L, -1, "ptr");
  setting = (void *)lua_topointer(L, -1);
  
  lua_pop(L, 2);
  return setting;
}

const shuso_setting_value_t *shuso_setting_check_value(shuso_t *S, const shuso_setting_block_t *block, const char *name, int nval) {
  const shuso_setting_t   *setting = shuso_setting(S, block, name);
  if(!setting) {
    shuso_set_error(S, "setting %s not found", name);
    return NULL;
  };
  if(!setting->values.merged) {
    shuso_set_error(S, "setting %s value not available", name);
    return NULL;
  }
  if(setting->values.merged->count <= nval) {
    shuso_set_error(S, "setting %s has %d but needed value #%s", name, (int )setting->values.merged->count, (int )nval);
    return NULL;
  }
  return &setting->values.merged->array[nval];
}

bool shuso_setting_check_boolean(shuso_t *S, const shuso_setting_block_t *block,  const char *name, int n, bool *ret) {
  const shuso_setting_value_t *val = shuso_setting_check_value(S, block, name, n);
  if(!val) return false;
  if(!val->valid.boolean) {
    return shuso_set_error(S, "setting %s value #%d is not a boolean", name, n);
  }
  if(ret) *ret = val->boolean;
  return true;
}
bool shuso_setting_check_integer(shuso_t *S, const shuso_setting_block_t *block,  const char *name, int n, int *ret) {
  const shuso_setting_value_t *val = shuso_setting_check_value(S, block, name, n);
  if(!val) return false;
  if(!val->valid.integer) {
    return shuso_set_error(S, "setting %s value #%d is not an integer", name, n);
  }
  if(ret) *ret = val->integer;
  return true;
}
bool shuso_setting_check_number(shuso_t *S, const shuso_setting_block_t *block,  const char *name, int n, double *ret) {
  const shuso_setting_value_t *val = shuso_setting_check_value(S, block, name, n);
  if(!val) return false;
  if(!val->valid.number) {
    return shuso_set_error(S, "setting %s value #%d is not a number", name, n);
  }
  if(ret) *ret = val->number;
  return true;
}
bool shuso_setting_check_string(shuso_t *S, const shuso_setting_block_t *block,  const char *name, int n, const char **ret) {
  const shuso_setting_value_t *val = shuso_setting_check_value(S, block, name, n);
  if(!val) return false;
  if(!val->valid.string) {
    return shuso_set_error(S, "setting %s value #%d is not a string", name, n);
  }
  if(ret) *ret = val->string;
  return true;
}

static bool config_init_config(shuso_t *S, shuso_module_t *self, shuso_setting_block_t *block){
  
  return true;
}

shuso_module_t shuso_config_module = {
  .name = "config",
  .version = "0.0.1",
  .initialize_config = config_init_config
};
