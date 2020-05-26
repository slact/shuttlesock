#include <shuttlesock.h>
#include "config.h"
#include "private.h"
#include <lauxlib.h>
#include <assert.h>

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
  
  
  
  luaS_pcall_config_method(Lworker, "all_settings", 0, 1);
  lua_pushnil(Lworker);
  while(lua_next(Lworker, -2)) {
    lua_getfield(Lworker, -1, "ptr");
    const shuso_setting_t *setting = lua_topointer(Lworker, -1);
    lua_pop(Lworker, 1);
    luaS_config_pointer_ref(Lworker, setting);
  }
  lua_pop(Lworker, 1);
  
  luaS_pcall_config_method(Lworker, "all_blocks", 0, 1);
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

static bool luaS_push_config_function(lua_State *L, const char *funcname) {
  shuso_t *S = shuso_state(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, S->config.index);
  if(lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_getglobal(L, "require");
    lua_pushliteral(L, "shuttlesock.core.config");
  }
  lua_call(L, 1, 1);
  lua_getfield(L, -1, funcname);
  lua_remove(L, -2);
  return true;
}

bool luaS_config_pointer_ref(lua_State *L, const void *ptr) {
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

bool luaS_pcall_config_method(lua_State *L, const char *method_name, int nargs, int nret) {
  shuso_t                    *S = shuso_state(L);
  int                         argstart = lua_absindex(L, -nargs);
  shuso_config_module_ctx_t  *ctx = S->common->module_ctx.config;
  
  luaS_get_config_pointer_ref(L, ctx);  
  lua_getfield(L, -1, method_name);
  lua_insert(L, argstart);
  lua_insert(L, argstart + 1);
  if(!luaS_pcall(L, nargs + 1, nret)) {
    return false;
  }
  if(nret >= 1 && lua_isnil(L, -nret)) {
    if(nret >= 2) {
      const char *err = lua_isstring(L, -1) ? lua_tostring(L, -1) : "returned nil with no error message, but probably kind of expected an error message";
      shuso_set_error(S, err);
    }
    return false;
  }
  return true;
}

bool shuso_config_register_setting(shuso_t *S, shuso_module_setting_t *setting, shuso_module_t *module) {
  lua_State *L = S->lua.state;
  int top = lua_gettop(L);
  lua_newtable(L);
  
  if(!setting->name) {
    lua_settop(L, top);
    return shuso_set_error(S, "module %s setting %p name cannot be NULL", module->name, setting);
  }
  
  lua_pushstring(L, setting->name);
  lua_setfield(L, -2, "name");
  
  lua_pushstring(L, setting->path);
  lua_setfield(L, -2, "path");
  
  if(!setting->nargs) {
    lua_settop(L, top);
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
  if(!luaS_pcall_config_method(L, "register_setting", 2, 2)) {
    lua_settop(L, top);
    return false;
  }
  lua_pop(L, 2);
  assert(lua_gettop(L) == top);
  return true;
}

bool shuso_config_system_initialize(shuso_t *S) {
  lua_State *L = S->lua.state;
  
  shuso_config_module_ctx_t *ctx = shuso_stalloc(&S->stalloc, sizeof(*ctx));
  if(!ctx) {
    return shuso_set_error(S, "failed to allocate module context");
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

static shuso_setting_t *lua_setting_to_c_struct(shuso_t *S, lua_State *L, int sidx) {
  sidx = lua_absindex(L, sidx);
  shuso_setting_t *setting = shuso_stalloc(&S->stalloc, sizeof(*setting));
  if(!setting) {
    shuso_set_error(S, "failed to alloc config setting");
    return NULL;
  }
  
  lua_getfield(L, sidx, "name");
  setting->name = lua_tostring(L, -1);
  lua_pop(L, 1);
  
  lua_getfield(L, sidx, "raw_name");
  setting->raw_name = lua_tostring(L, -1);
  lua_pop(L, 1);
  
  lua_pushlightuserdata(L, setting);
  lua_setfield(L, sidx, "ptr");
  
  lua_pushvalue(L, sidx);
  luaS_pcall_config_method(L, "get_path", 1, 1);
  setting->path = lua_tostring(L, -1);
  lua_pop(L, 1);
  
  lua_getfield(L, sidx, "module");
  if(lua_istable(L, -1)) {
    lua_getfield(L, -1, "name");
    lua_remove(L, -2);
  }
  setting->module = lua_tostring(L, -1);
  lua_pop(L, 1);
  
  struct {
    const char *name;
    shuso_instrings_t **instrings;
  } ins[] = {
    {"local", &setting->instrings.local},
    {"default", &setting->instrings.defaults},
    {"inherited", &setting->instrings.inherited},
  };
  
  for(int i=0; i<3; i++) {
    assert(lua_istable(L, sidx));
    
    lua_pushvalue(L, sidx);
    lua_pushstring(L, ins[i].name);
    luaS_pcall_config_method(L, "setting_instrings", 2, 1);
    assert(lua_istable(L, -1));
    
    int cnt = shuso_error_capture_start(S);
    *ins[i].instrings = luaS_instrings_lua_to_c(L, setting, -1);
    if(shuso_error_capture_finish(S, cnt)) {
      shuso_config_error(S, setting, shuso_last_error(S));
      return false;
    }
    assert(*ins[i].instrings != NULL);
    lua_pop(L, 1);
  }
  
  if(setting->instrings.local->count > 0) {
    setting->instrings.merged = setting->instrings.local;
  }
  else if(setting->instrings.inherited->count > 0) {
    setting->instrings.merged = setting->instrings.inherited;
  }
  else {
    setting->instrings.merged = setting->instrings.defaults;
  }
  assert(setting->instrings.merged != NULL);
  
  lua_getfield(L, sidx, "parent");
  assert(!lua_isnil(L, -1));
  lua_getfield(L, -1, "block");
  assert(!lua_isnil(L, -1));
  lua_getfield(L, -1, "ptr");
  assert(!lua_isnil(L, -1));
  setting->parent_block = (void *)lua_topointer(L, -1);
  lua_pop(L, 3);
  
  lua_getfield(L, -1, "block");
  if(lua_isnil(L, -1)) {
    lua_pop(L, 1);
    setting->block = NULL;
  }
  else {
    shuso_setting_block_t *block = shuso_stalloc(&S->stalloc, sizeof(*block));
    if(block == NULL) {
      shuso_set_error(S, "failed to allocate memory for config block");
      return NULL;
    }
    lua_getfield(L, -1, "ptr");
    assert(lua_isnil(L, -1));
    lua_pop(L, 1);
    lua_pushlightuserdata(L, block);
    lua_setfield(L, -2, "ptr");
    
    lua_pushvalue(L, -1);
    luaS_pcall_config_method(L, "get_path", 1, 1);
    block->path = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    luaS_config_pointer_ref(L, block); //also pops the block table
    setting->block = block;
    block->setting = setting;
    if(!shuso_context_list_initialize(S, NULL, &block->context_list, &S->stalloc)) {
      shuso_set_error(S, "unable to initialize config block module context list");
      return NULL;
    }
  }
  
  luaS_config_pointer_ref(L, setting); //pops the setting table too
  return setting;
}

bool shuso_config_system_generate(shuso_t *S) {
  lua_State *L = S->lua.state;
  int top = lua_gettop(L);
  luaL_checkstack(L, 5, NULL);
  
  luaS_push_config_field(L, "parsed");
  if(!lua_toboolean(L, -1)) {
    if(!shuso_configure_string(S, "empty default", "")) {
      lua_settop(L, top);
      return false;
    }
  }
  lua_pop(L, 1);
  
  if(!luaS_pcall_config_method(L, "handle", 0, 2)) {
    lua_settop(L, top);
    return false;
  }
  lua_pop(L, 1); //pop 2nd return val
  
  //create C structs for root
  if(!luaS_pcall_config_method(L, "get_root", 0, 2)) {
    lua_settop(L, top);
    return false;
  }
  lua_pop(L, 1); //pop 2nd return val
  
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
  
  lua_pushvalue(L, -1);
  luaS_pcall_config_method(L, "get_path", 1, 0); //create and cache the path -- don't care about the result
  
  luaS_config_pointer_ref(L, root_block); //also pops the block table
  
  shuso_setting_t *root_setting = shuso_stalloc(&S->stalloc, sizeof(*root_setting));
  if(!root_setting) {
    lua_settop(L, top);
    return shuso_set_error(S, "failed to allocate memory for config root");
  }
  *root_setting = (shuso_setting_t ) {
    .name = "::ROOT",
    .instrings = {
      .local = NULL,
      .inherited = NULL,
      .defaults = NULL,
      .merged = NULL
    },
    .path="",
    .block = root_block,
    .parent_block = NULL
  };
  
  lua_pushlightuserdata(L, root_setting);
  lua_setfield(L, -2, "ptr");
  
  lua_pushvalue(L, -1);
  luaS_pcall_config_method(L, "get_path", 1, 1);
  root_setting->path = lua_tostring(L, -1);
  assert(root_setting->path);
  lua_pop(L, 1);
  
  root_block->setting = root_setting;
  luaS_config_pointer_ref(L, root_setting); //pops the root setting table too
  
  //finished setting up root structs
  
  //create C structs from config settings
  luaS_pcall_config_method(L, "all_settings", 0, 1);
  assert(lua_istable(L, -1));
  for(int i=0, num_settings = luaL_len(L, -1); i<num_settings; i++) {
    lua_rawgeti(L, -1, i+1);
    if(lua_setting_to_c_struct(S, L) == NULL) {
      lua_settop(L, top);
      return false;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  
  //walk the blocks again, and let the modules set their settings for each block
  luaS_pcall_config_method(L, "all_blocks", 0, 1);
  assert(lua_istable(L, -1));
  for(int i=0, num_blocks = luaL_len(L, -1); i<num_blocks; i++) {
    shuso_setting_block_t      *block;
    
    lua_geti(L, -1, i+1);
    assert(lua_istable(L, -1));
    lua_getfield(L, -1, "ptr");
    block = (void *)lua_topointer(L, -1);
    lua_pop(L, 2); //pop [i].ptr
    assert(block != NULL);
    
    for(unsigned j=0; j < S->common->modules.count; j++) {
      shuso_module_t *module = S->common->modules.array[j];
      int errcount = shuso_error_count(S);
      if(module->initialize_config && !module->initialize_config(S, module, block)) {
        if(shuso_error_count(S) == errcount) {
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
  }
  lua_pop(L, 1);
  
  shuso_config_module_ctx_t  *ctx = S->common->module_ctx.config;
  
  assert(ctx->blocks.root == NULL);
  
  if(!luaS_pcall_config_method(L, "get_root", 0, 2)) {
    lua_settop(L, top);
    return false;
  }
  lua_pop(L, 1); //2nd return from get_root
  
  lua_getfield(L, -1, "ptr");
  ctx->blocks.root = lua_topointer(L, -1);
  assert(ctx->blocks.root);
  lua_pop(L, 2);
  
  
  //create config block array
  luaS_pcall_config_method(L, "all_blocks", 0, 1);
  assert(lua_istable(L, -1));
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
  
  if(lua_gettop(L) != top) {
    shuso_log_warning(S, "Lua stack grew by %d after shuso_config_system_generate", top - lua_gettop(L));
    lua_settop(L, top);
  }
  return true;
}
shuso_setting_block_t *shuso_setting_parent_block(shuso_t *S, const shuso_setting_t *setting) {
  return setting->parent_block;
}

shuso_setting_t *shuso_setting(shuso_t *S, const shuso_setting_block_t *block, const char *name) {
  lua_State         *L = S->lua.state;
  shuso_setting_t   *setting;
  lua_pushstring(L, name);
  luaS_get_config_pointer_ref(L, block->setting);
  if(!luaS_pcall_config_method(L, "find_setting", 2, 1)) {
    lua_pop(L, 1);
    return NULL;
  }
  lua_getfield(L, -1, "ptr");
  setting = (void *)lua_topointer(L, -1);
  
  lua_pop(L, 2);
  return setting;
}

bool shuso_setting_value(shuso_t *S, const shuso_setting_t *setting, size_t nval, shuso_setting_value_merge_type_t mergetype, shuso_setting_value_type_t valtype, void *ret) {
  if(!setting) {
    return false;
  }
  shuso_instrings_t *instrings;
  switch(mergetype) {
    case SHUSO_SETTING_MERGED:
      instrings = setting->instrings.merged;
      break;
    case SHUSO_SETTING_LOCAL:
      instrings = setting->instrings.local;
      break;
    case SHUSO_SETTING_INHERITED:
      instrings = setting->instrings.inherited;
      break;
    case SHUSO_SETTING_DEFAULT:
      instrings = setting->instrings.defaults;
      break;
    default:
      shuso_set_error(S, "Fatal API error: invalid setting mergetype %d, shuso_setting_value() was probably called incorrectly", mergetype);
      raise(SIGABRT);
      return false;
  }
  
  if(!instrings || instrings->count > nval) {
    return false;
  }
  shuso_instring_t *instring = &instrings->array[nval];
  
  switch(valtype) {
    case SHUSO_SETTING_BOOLEAN: 
      return shuso_instring_boolean_value(S, instring, ret);
    case SHUSO_SETTING_INTEGER:
      return shuso_instring_integer_value(S, instring, ret);
    case SHUSO_SETTING_NUMBER:
      return shuso_instring_number_value(S, instring, ret);
    case SHUSO_SETTING_SIZE:
      return shuso_instring_size_value(S, instring, ret);
    case SHUSO_SETTING_STRING:
      return shuso_instring_string_value(S, instring, ret);
    case SHUSO_SETTING_BUFFER:
      return shuso_instring_buffer_value(S, instring, ret);
    default:
      shuso_set_error(S, "Fatal API error: invalid setting type %d, shuso_setting_value() was probably called incorrectly", mergetype);
      raise(SIGABRT);
      return false;
  }
}

bool shuso_setting_boolean(shuso_t *S, shuso_setting_t *setting, int n, bool *ret) {
  return shuso_setting_value(S, setting, n, SHUSO_SETTING_MERGED, SHUSO_SETTING_BOOLEAN, ret);
}
bool shuso_setting_integer(shuso_t *S, shuso_setting_t *setting, int n, int *ret) {
  return shuso_setting_value(S, setting, n, SHUSO_SETTING_MERGED, SHUSO_SETTING_INTEGER, ret);
}
bool shuso_setting_number(shuso_t *S, shuso_setting_t *setting, int n, double *ret) {
  return shuso_setting_value(S, setting, n, SHUSO_SETTING_MERGED, SHUSO_SETTING_NUMBER, ret);
}
bool shuso_setting_size(shuso_t *S, shuso_setting_t *setting, int n, size_t *ret) {
  return shuso_setting_value(S, setting, n, SHUSO_SETTING_MERGED, SHUSO_SETTING_SIZE, ret);
}
bool shuso_setting_string(shuso_t *S, shuso_setting_t *setting, int n, shuso_str_t *ret) {
  return shuso_setting_value(S, setting, n, SHUSO_SETTING_MERGED, SHUSO_SETTING_STRING, ret);
}
bool shuso_setting_buffer(shuso_t *S, shuso_setting_t *setting, int n, const shuso_buffer_t **ret) {
  return shuso_setting_value(S, setting, n, SHUSO_SETTING_MERGED, SHUSO_SETTING_BUFFER, ret);
}
bool shuso_setting_string_matches(shuso_t *S, shuso_setting_t *setting, int n, const char *lua_matchstring) {
  lua_State *L = S->lua.state;
  int top = lua_gettop(L);
  if(!setting || !setting->instrings.merged || setting->instrings.merged->count < (size_t)n) {
    return false;
  }
  shuso_instring_t *instring = &setting->instrings.merged->array[n];
  if(!shuso_instring_string_value(S, instring, NULL)) {
    return false;
  }
  
  lua_reference_t string_ref = instring->cached_value.string_lua_ref;
  assert(string_ref != LUA_NOREF);
  
  if(lua_checkstack(L, 3)) {
    return false;
  }
  luaS_push_lua_module_field(L, "string", "match");
  lua_rawgeti(L, LUA_REGISTRYINDEX, string_ref);
  assert(lua_isstring(L, -1));
  lua_pushstring(L, lua_matchstring);
  lua_call(L, 2, 1);
  bool matched = lua_toboolean(L, -1);
  lua_pop(L, 1);
  assert(lua_gettop(L) == top);
  return matched;
}


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

bool shuso_configure_string(shuso_t *S,  const char *str_title, const char *str) {
    lua_State                   *L = S->lua.state;
  shuso_config_module_ctx_t   *ctx = S->common->module_ctx.config;
  
  lua_pushcfunction(L, luaS_passthru_error_handler);
  int errhandler_index = lua_gettop(L);
  
  luaS_get_config_pointer_ref(L, ctx);
  lua_getfield(L, -1, "parse");
  lua_insert(L, -2);
  lua_pushstring(L, str);
  lua_newtable(L);
  if(str_title) {
    lua_pushstring(L, str_title);
    lua_setfield(L, -2, "name");
  }
  int ret = lua_pcall(L, 3, 0, errhandler_index);
  lua_remove(L, errhandler_index);
  if(ret != LUA_OK) {
    shuso_set_error(S, "%s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return false;
  }
  return true;
}

size_t __shuso_setting_values_count(shuso_t *S, const shuso_setting_t *setting, shuso_setting_value_merge_type_t mt) {
  shuso_instrings_t *ins;
  switch(mt) {
    case SHUSO_SETTING_MERGED:
      ins = setting->instrings.merged;
      break;
    case SHUSO_SETTING_LOCAL:
      ins = setting->instrings.local;
      break;
    case SHUSO_SETTING_INHERITED:
      ins = setting->instrings.inherited;
      break;
    case SHUSO_SETTING_DEFAULT:
      ins = setting->instrings.defaults;
      break;
    default:
      shuso_set_error(S, "Fatal API error: invalid setting mergetype %d, shuso_setting_values_count() was probably called incorrectly", mt);
      raise(SIGABRT);
      return 0;
  }
  return ins->count;
}

static bool shuso_config_match_thing_path(shuso_t *S, const void *thing, const char *path) {
  lua_State *L = S->lua.state;
  luaS_get_config_pointer_ref(L, thing);
  assert(!lua_isnil(L, -1));
  luaS_push_lua_module_field(L, "shuttlesock.core.config", "match_path");
  lua_insert(L, -2);
  lua_pushstring(L, path);
  luaS_call(L, 2, 1);
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
  int top = lua_gettop(L);
  lua_pushlightuserdata(L, (void *)ptr);
  va_list args_again;
  va_copy(args_again, args);
  int errlen = vsnprintf(NULL, 0, fmt, args);
  luaL_Buffer buf;
  vsnprintf(luaL_buffinitsize(L, &buf, errlen + 1), errlen + 1, fmt, args_again);
  va_end(args_again);
  luaL_pushresultsize(&buf, errlen);
  luaS_pcall_config_method(L, "error", 2, 1);
  const char *lerr = lua_tostring(L, -1);
  shuso_set_error(S, lerr);
  lua_settop(L, top);
  return false;
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

static bool config_initialize(shuso_t *S, shuso_module_t *self) {
  lua_State *L = S->lua.state;
  lua_getglobal(L, "require");
  lua_pushliteral(L, "shuttlesock.core.config");
  lua_call(L, 1, 1);
  S->config.index = luaL_ref(L, LUA_REGISTRYINDEX);
  
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
