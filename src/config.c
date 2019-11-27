#include <shuttlesock.h>
#include <lauxlib.h>

static bool luaS_push_config_function(lua_State *L, const char *funcname) {
  lua_getglobal(L, "require");
  lua_pushliteral(L, "shuttlesock.core.config");
  lua_call(L, 1, 1);
  lua_getfield(L, -1, funcname);
  lua_remove(L, -2);
  return true;
}

static bool luaS_config_pointer_ref(lua_State *L, const void *ptr) {
  luaS_pointer_ref(L, "shuttlesock.config.pointer_ref_table", ptr);
  return true;
}

bool luaS_get_config_pointer_ref(lua_State *L, const void *ptr) {
  luaS_get_pointer_ref(L, "shuttlesock.config.pointer_ref_table", ptr);
  return lua_isnil(L, -1);
}

void luaS_push_config_field(lua_State *L, const char *field) {
  shuso_t                    *S = shuso_state(L);
  shuso_config_module_ctx_t  *ctx = S->common->module_ctx.config;
  luaS_get_config_pointer_ref(L, ctx);
  lua_getfield(L, -1, field);
  lua_remove(L, -2);
}

bool luaS_pcall_config_method(lua_State *L, const char *method_name, int nargs, bool keep_result) {
  shuso_t                    *S = shuso_state(L);
  int                         argstart = lua_absindex(L, -nargs);
  shuso_config_module_ctx_t  *ctx = S->common->module_ctx.config;
  
  luaS_get_config_pointer_ref(L, ctx);
  
  lua_getfield(L, -1, method_name);
  lua_insert(L, argstart);
  lua_insert(L, argstart + 1);
  return luaS_function_pcall_result_ok(L, nargs + 1, keep_result);
}

shuso_module_setting_t SHUTTLESOCK_SETTINGS_END = {
  .name = NULL,
};

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
  
  luaS_config_pointer_ref(L, ctx);
  *ctx =(shuso_config_module_ctx_t ) { 0 };
  
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
    lua_getfield(L, -1, "value");
    shuso_setting_value_t *val = &v->array[i-1];
    
    lua_getfield(L, -1, "raw");
    if(lua_isstring(L, -1)) {
      val->raw = lua_tolstring(L, -1, &val->raw_len);
    }
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "string");
    if(lua_isstring(L, -1)) {
      val->valid.string = true;
      val->string = lua_tolstring(L, -1, &val->string_len);
    }
    else {
      val->valid.string = false;
    }
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "number");
    if(lua_isnumber(L, -1)) {
      val->valid.number = true;
      val->number = lua_tonumber(L, -1);
    }
    else {
      val->valid.number = false;
    }
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "integer");
    if(lua_isinteger(L, -1)) {
      val->valid.integer = true;
      val->integer = lua_tointeger(L, -1);
    }
    else {
      val->valid.integer = false;
    }
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "boolean");
    if(lua_isboolean(L, -1)) {
      val->valid.boolean = true;
      val->boolean = lua_toboolean(L, -1);
    }
    else {
      val->valid.boolean = false;
    }
    lua_pop(L, 1);
    
    lua_pop(L, 2);
  }
  return v;
}

bool shuso_config_system_generate(shuso_t *S) {
  lua_State *L = S->lua.state;
  int top = lua_gettop(L);
  luaS_push_config_field(L, "parsed");
  if(!lua_toboolean(L, -1)) {
    lua_pop(L, 1);
    if(!shuso_configure_string(S, "", "empty default")) {
      lua_settop(L, top);
      return false;
    }
  }
  
  if(!luaS_pcall_config_method(L, "handle", 0, false)) {
    lua_settop(L, top);
    return false;
  }
  
  //create C structs for root
  if(!luaS_pcall_config_method(L, "get_root", 0, true)) {
    lua_settop(L, top);
    return false;
  }
  shuso_setting_block_t *root_block = shuso_stalloc(&S->stalloc, sizeof(*root_block));
  if(root_block == NULL) {
    lua_settop(L, top);
    return shuso_set_error(S, "failed to allocate memory for config root block");
  }
  lua_getfield(L, -1, "block");
  *root_block = (shuso_setting_block_t ) {
    .setting = NULL,
    .path="",
  };
  
  if(!shuso_context_list_initialize(S, NULL, &root_block->context_list, &S->stalloc)) {
    lua_settop(L, top);
    return shuso_set_error(S, "unable to initialize root block module context list");
  }
  lua_pushlightuserdata(L, root_block);
  lua_setfield(L, -2, "ptr");
  
  luaS_config_pointer_ref(L, root_block); //also pops the block table
  
  shuso_setting_t *root_setting = shuso_stalloc(&S->stalloc, sizeof(*root_setting));
  if(!root_setting) {
    lua_settop(L, top);
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
    .path="",
    .block = root_block
  };
  
  lua_pushlightuserdata(L, root_setting);
  lua_setfield(L, -2, "ptr");
  
  root_block->setting = root_setting;
  luaS_config_pointer_ref(L, root_setting); //pops the root setting table too
  
  //finished setting up root structs
  
  
  if(!luaS_pcall_config_method(L, "all_settings", 0, true)) {
    lua_settop(L, top);
    return false;
  }
  //create C structs from config settings
  lua_pushnil(L);
  while(lua_next(L, -2)) {
    shuso_setting_t *setting = shuso_stalloc(&S->stalloc, sizeof(*setting));
    if(!setting) {
      lua_settop(L, top);
      return shuso_set_error(S, "failed to alloc config setting");
    }
    
    lua_getfield(L, -1, "name");
    setting->name = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "raw_name");
    setting->raw_name = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    lua_pushlightuserdata(L, setting);
    lua_setfield(L, -2, "ptr");
    
    lua_pushvalue(L, -1);
    luaS_pcall_config_method(L, "get_path", 1, true);
    setting->path = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "module");
    if(lua_istable(L, -1)) {
      lua_getfield(L, -1, "name");
      lua_remove(L, -2);
    }
    setting->module = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "values");
    setting->values.local = lua_setting_values_to_c_struct(L, &S->stalloc);
    if(!setting->values.local) {
      lua_settop(L, top);
      return false;
    }
    lua_pop(L, 1);
    
    lua_pushvalue(L, -1);
    if(!luaS_pcall_config_method(L, "default_values", 1, true)) {
      lua_settop(L, top);
      return false;
    }
    setting->values.defaults = lua_setting_values_to_c_struct(L, &S->stalloc);
    if(!setting->values.defaults) {
      lua_settop(L, top);
      return false;
    }
    lua_pop(L, 1);
    
    lua_pushvalue(L, -1);
    if(!luaS_pcall_config_method(L, "inherited_values", 1, true)) {
      lua_settop(L, top);
      return false;
    }
    setting->values.inherited = lua_setting_values_to_c_struct(L, &S->stalloc);
    if(!setting->values.inherited) {
      lua_settop(L, top);
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
      lua_settop(L, top);
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
        lua_settop(L, top);
        return shuso_set_error(S, "failed to allocate memory for config block");
      }
      lua_getfield(L, -1, "ptr");
      assert(lua_isnil(L, -1));
      lua_pop(L, 1);
      lua_pushlightuserdata(L, block);
      lua_setfield(L, -2, "ptr");
      
      lua_pushvalue(L, -1);
      luaS_pcall_config_method(L, "get_path", 1, true);
      block->path = lua_tostring(L, -1);
      lua_pop(L, 1);
      
      luaS_config_pointer_ref(L, block); //also pops the block table
      setting->block = block;
      block->setting = setting;
      if(!shuso_context_list_initialize(S, NULL, &block->context_list, &S->stalloc)) {
        lua_settop(L, top);
        return shuso_set_error(S, "unable to initialize config block module context list");
      }
    }
    
    luaS_config_pointer_ref(L, setting); //pops the setting table too
  }
  lua_pop(L, 1);
  
  
  //walk the blocks again, and let the modules set their settings for each block
  if(!luaS_pcall_config_method(L, "all_blocks", 0, true)) {
    lua_settop(L, top);
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
      int errcount = shuso_error_count(S);
      if(module->initialize_config && !module->initialize_config(S, module, block)) {
        if(shuso_last_error(S) == NULL) {
          lua_settop(L, top);
          return shuso_set_error(S, "module %s failed to initialize config, but reported no error", module->name);
        }
        lua_settop(L, top);
        return false;
      }
      if(shuso_error_count(S) > errcount) {
        lua_settop(L, top);
        return false;
      }
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  shuso_config_module_ctx_t  *ctx = S->common->module_ctx.config;
  
  assert(ctx->blocks.root == NULL);
  
  if(!luaS_pcall_config_method(L, "get_root", 0, true)) {
    lua_settop(L, top);
    return false;
  }
  lua_getfield(L, -1, "ptr");
  ctx->blocks.root = lua_topointer(L, -1);
  assert(ctx->blocks.root);
  lua_pop(L, 2);
  
  if(!luaS_pcall_config_method(L, "all_blocks", 0, true)) {
    lua_settop(L, top);
    return false;
  }
  ctx->blocks.count = luaL_len(L, -1);
  ctx->blocks.array = shuso_stalloc(&S->stalloc, sizeof(*ctx->blocks.array) * ctx->blocks.count);
  if(ctx->blocks.array == NULL) {
    lua_settop(L, top);
    return shuso_set_error(S, "failed to allocate config block array");
  }
  lua_pushnil(L);
  while(lua_next(L, -2)) {
    int i = lua_tointeger(L, -2) - 1;
    lua_getfield(L, -1, "ptr");
    ctx->blocks.array[i] = lua_topointer(L, -1);
    lua_pop(L, 2);
  }
  lua_pop(L, 1);
  
  return true;
}

shuso_setting_t *shuso_setting(shuso_t *S, const shuso_setting_block_t *block, const char *name) {
  lua_State         *L = S->lua.state;
  shuso_setting_t   *setting;
  lua_pushstring(L, name);
  luaS_get_config_pointer_ref(L, block->setting);
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


static void config_worker_gxcopy(shuso_t *S, shuso_event_state_t *evs, intptr_t code, void *data, void *pd) {
  assert(strcmp(evs->data_type, "shuttlesock_state") == 0);
  shuso_t         *Smanager = data;
  lua_State       *Lworker = S->lua.state;
  lua_State       *Lmanager = Smanager->lua.state;
  
  luaS_get_config_pointer_ref(Lmanager, Smanager->common->module_ctx.config);
  if(!luaS_gxcopy(Lmanager, Lworker)) {
    shuso_set_error(S, "failed to copy Lua config stuff");
    lua_pop(Lmanager, 1);
    return;
  }
  lua_pop(Lmanager, 1);

  assert(lua_istable(Lworker, -1));
  
  luaS_config_pointer_ref(Lworker, S->common->module_ctx.config);
  
  //copy ptr => setting/block/whatever mappings
  
  
  
  luaS_pcall_config_method(Lworker, "all_settings", 0, true);
  lua_pushnil(Lworker);
  while(lua_next(Lworker, -2)) {
    lua_getfield(Lworker, -1, "ptr");
    const shuso_setting_t *setting = lua_topointer(Lworker, -1);
    lua_pop(Lworker, 1);
    luaS_config_pointer_ref(Lworker, setting);
  }
  lua_pop(Lworker, 1);
  
  luaS_pcall_config_method(Lworker, "all_blocks", 0, true);
  lua_pushnil(Lworker);
  while(lua_next(Lworker, -2)) {
    lua_getfield(Lworker, -1, "ptr");
    const shuso_setting_block_t *block = lua_topointer(Lworker, -1);
    lua_pop(Lworker, 1);
    luaS_config_pointer_ref(Lworker, block);
  }
  lua_pop(Lworker, 1);
  
  return;
}

static bool config_initialize(shuso_t *S, shuso_module_t *self) {
  shuso_event_listen(S, "core:worker.start.before.lua_gxcopy", config_worker_gxcopy, self);
  return true;
}

static bool config_init_config(shuso_t *S, shuso_module_t *self, shuso_setting_block_t *block){
  return true;
}

shuso_module_t shuso_config_module = {
  .name = "config",
  .version = SHUTTLESOCK_VERSION_STRING,
  .subscribe = "core:worker.start.before.lua_gxcopy",
  .initialize = config_initialize,
  .initialize_config = config_init_config,
};


bool shuso_configure_file(shuso_t *S, const char *path) {
  lua_State                   *L = S->lua.state;
  shuso_config_module_ctx_t   *ctx = S->common->module_ctx.config;
  
  lua_pushcfunction(L, luaS_passthru_error_handler);
  int errhandler_index = lua_gettop(L);
  
  luaS_get_config_pointer_ref(L, ctx);
  lua_getfield(L, -1, "load");
  lua_insert(L, -2);
  lua_pushstring(L, path);
  int ret = lua_pcall(L, 2, 0, errhandler_index);
  lua_remove(L, errhandler_index);
  if(ret != LUA_OK) {
    shuso_set_error(S, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return false;
  }
  return true;
}

bool shuso_configure_string(shuso_t *S,  const char *str, const char *str_title) {
    lua_State                   *L = S->lua.state;
  shuso_config_module_ctx_t   *ctx = S->common->module_ctx.config;
  
  lua_pushcfunction(L, luaS_passthru_error_handler);
  int errhandler_index = lua_gettop(L);
  
  luaS_get_config_pointer_ref(L, ctx);
  lua_getfield(L, -1, "parse");
  lua_insert(L, -2);
  lua_pushstring(L, str);
  if(str_title) {
    lua_pushstring(L, str_title);
  }
  int ret = lua_pcall(L, str_title ? 3 : 2, 0, errhandler_index);
  lua_remove(L, errhandler_index);
  if(ret != LUA_OK) {
    shuso_set_error(S, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return false;
  }
  return true;
}

static bool shuso_config_match_thing_path(shuso_t *S, const void *thing, const char *path) {
  lua_State *L = S->lua.state;
  luaS_get_config_pointer_ref(L, thing);
  assert(!lua_isnil(L, -1));
  luaS_push_lua_module_field(L, "shuttlesock.config", "match_path");
  lua_insert(L, -2);
  lua_pushstring(L, path);
  lua_call(L, 2, 1);
  bool matched = lua_toboolean(L, -1);
  lua_pop(L, 1);
  return matched;
}
bool shuso_config_match_setting_path(shuso_t *S, const shuso_setting_t *setting, const char *path) {
  return shuso_config_match_thing_path(S, setting, path);
}

bool shuso_config_match_block_path(shuso_t *S, const shuso_setting_block_t *block, const char *path) {
  return shuso_config_match_thing_path(S, block, path);
}

static bool shuso_config_error_va(shuso_t *S, const void *ptr, const char *fmt, va_list args) {
  lua_State *L = S->lua.state;
  lua_pushlightuserdata(L, (void *)ptr);
  va_list args_again;
  va_copy(args_again, args);
  int errlen = vsnprintf(NULL, 0, fmt, args);
  luaL_Buffer buf;
  vsnprintf(luaL_buffinitsize(L, &buf, errlen), errlen, fmt, args_again);
  va_end(args_again);
  luaL_pushresultsize(&buf, errlen);
  
  luaS_pcall_config_method(L, "error", 2, 1);
  size_t sz;
  const char *lerr = lua_tolstring(L, -1, &sz);
  char *err = shuso_stalloc(&S->stalloc, sz + 1);
  strcpy(err, lerr);
  lua_pop(L, 1);
  return lerr;
}

bool shuso_config_setting_error(shuso_t *S, shuso_setting_t *s, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  bool ret = shuso_config_error_va(S, s, fmt, args);
  va_end(args);
  return ret;
}
bool shuso_config_block_error(shuso_t *S, shuso_setting_block_t *b, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  bool ret = shuso_config_error_va(S, b, fmt, args);
  va_end(args);
  return ret;
}






