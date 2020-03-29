#include <shuttlesock.h>
#include <shuttlesock/modules/config/private.h>
#include <shuttlesock/embedded_lua_scripts.h>
#include <lauxlib.h>

static bool shuso_module_freeze(shuso_t *S, shuso_module_t *mod);
static bool shuso_module_finalize(shuso_t *S, shuso_module_t *mod);

const char *core_lua_modules[] = {
  NULL
};

static bool add_module(shuso_t *S, shuso_module_t *module, const char *adding_function_name) {
  if(!(shuso_runstate_check(S, SHUSO_STATE_CONFIGURING, "add module"))) {
    return false;
  }
  lua_State *L = S->lua.state;
  
  luaS_push_lua_module_field(L, "shuttlesock.core.module", adding_function_name);
  if(!module->name) {
    return shuso_set_error(S, "module name missing");
  }
  if(!module->version) {
    return shuso_set_error(S, "module %s version string is missing", module->name);
  }
  
  lua_pushstring(L, module->name);
  lua_pushlightuserdata(L, module);
  lua_pushstring(L, module->version);
  lua_pushstring(L, module->subscribe);
  lua_pushstring(L, module->publish); 
  lua_pushstring(L, module->parent_modules); 
  if(!luaS_function_call_result_ok(L, 6, true)) {
    S->common->state = SHUSO_STATE_MISCONFIGURED;
    return false;
  }
  
  lua_getfield(L, -1, "index");
  module->index = lua_tointeger(L, -1) - 1; //global module number
  lua_pop(L, 2);
  
  if(S->common->modules.count <= (size_t )module->index) {
    assert(S->common->modules.count == (size_t )module->index);
    S->common->modules.count++;
    S->common->modules.array = realloc(S->common->modules.array, sizeof(*S->common->modules.array) * S->common->modules.count);
    if(S->common->modules.array == NULL) {
      shuso_set_error(S, "failed to allocate memory for modules array");
      S->common->state = SHUSO_STATE_MISCONFIGURED;
      return false;
    }
    S->common->modules.array[module->index] = module;
  }
  
  //register the config settings
  if(module->settings) {
    for(shuso_module_setting_t *setting = &module->settings[0]; setting->name != NULL; setting++) {
      if(!shuso_config_register_setting(S, setting, module)){
        return false;
      }
    }
  }
  
  return true;
}

bool shuso_add_module(shuso_t *S, shuso_module_t *module) {
  return add_module(S, module, "new");
}

bool shuso_add_core_modules(shuso_t *S, char *errbuf, size_t errbuflen) {
  lua_State *L = S->lua.state;
  int top = lua_gettop(L);
  for(unsigned i=0; i<SHUTTLESOCK_CORE_MODULES_COUNT; i++) {
    int prepared_lua_args = 0;
    if(shuttlesock_core_modules[i].prepare_function) {
      shuttlesock_core_modules[i].prepare_function(S, NULL);
      prepared_lua_args = lua_gettop(L) - top;
    }
    
    if(shuttlesock_core_modules[i].module) {
      if(!add_module(S, shuttlesock_core_modules[i].module, i > 0 ? "new" : "new_core_module")) {
        const char *lasterr = shuso_last_error(S);
        if(lasterr) {
          snprintf(errbuf, errbuflen, "failed to add core module \"%s\": %s", shuttlesock_core_modules[i].name, lasterr);
        }
        else {
          snprintf(errbuf, errbuflen, "failed to add core module \"%s\"", shuttlesock_core_modules[i].name);
        }
        lua_settop(L, top);
        return false;
      }
    }
    if(shuttlesock_core_modules[i].lua_script) {
      luaS_do_embedded_script(L, shuttlesock_core_modules[i].lua_script, prepared_lua_args);
      if(lua_gettop(L) > top && lua_isnil(L, -1)) {
        lua_pop(L, 1);
      }
    }
    if(shuttlesock_core_modules[i].lua_require) {
      lua_getglobal(L, "require");
      lua_pushstring(L, shuttlesock_core_modules[i].lua_require);
      if(!luaS_call_noerror(L, 1, 1)) {
        snprintf(errbuf, errbuflen, "failed to add core Lua module \"%s\": %s", shuttlesock_core_modules[i].name, lua_tostring(L, -1));
        lua_settop(L, top);
        return false;
      }
      if(lua_gettop(L) > top && lua_isnil(L, -1)) {
        lua_pop(L, 1);
      }
    }
    if(shuttlesock_core_modules[i].lua_module) {
      int module_stack_index = lua_gettop(L) >= top+1 && lua_istable(L, top+1) ? top+1 : 0;

      lua_getglobal(L, "require");
      lua_pushliteral(L, "shuttlesock.module");
      lua_call(L, 1, 1);
      lua_getfield(L, -1, "find");
      lua_pushstring(L, shuttlesock_core_modules[i].name);
      lua_call(L, 1, 1);
      if(lua_toboolean(L, -1)) {
        //lua module has already been added
        lua_settop(L, top);
      }
      else {
        lua_pop(L, 1);
        if(module_stack_index == 0) {
          snprintf(errbuf, errbuflen, "failed to add core Lua module \"%s\": script or required module didn't return a module table", shuttlesock_core_modules[i].name);
          lua_settop(L, top);
          return false;
        } 
        else {
          lua_getfield(L, -1, "is_lua_module");
          lua_pushvalue(L, top+1);
          lua_call(L, 1, 1);
          if(lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_getfield(L, -1, "new");
            lua_pushvalue(L, top+1);
            if(lua_pcall(L, 1, 2, 0) != LUA_OK) {
              snprintf(errbuf, errbuflen, "failed to add core Lua module \"%s\": %s", shuttlesock_core_modules[i].name, lua_tostring(L, -1));
              lua_settop(L, top);
              return false;
            }
            lua_pop(L, 2);
          }
          lua_pop(L, 1);
        }
      }
    }
    lua_settop(L, top);
  }
  return true;
}

bool shuso_initialize_added_modules(shuso_t *S) {
  lua_State *L = S->lua.state;
  if(S->common->modules.events == NULL) {
    S->common->modules.events = shuso_stalloc(&S->stalloc, sizeof(void *) * S->common->modules.count);
  }
  if(S->common->modules.events == NULL) {
    return shuso_set_error(S, "failed to allocate memory for modules' events array");
  }
  
  for(unsigned i=0; i<S->common->modules.count; i++) {
    shuso_module_t *module = S->common->modules.array[i];
    if(!shuso_module_freeze(S, module)) {
      return false;
    }
  }
  
  for(unsigned i=0; i<S->common->modules.count; i++) {
    shuso_module_t *module = S->common->modules.array[i];
    luaS_push_lua_module_field(L, "shuttlesock.core.module", "start_initializing_module");
    lua_pushstring(L, module->name);
    if(!luaS_function_call_result_ok(L, 1, false)) {
      return false;
    }
    if(module->initialize) {
      int errcount = shuso_error_count(S);
      if(!module->initialize(S, module)) {
        if(shuso_last_error(S) == NULL) {
          return shuso_set_error(S, "module %s failed to initialize, but reported no error", module->name);
        }
        return false;
      }
      if(shuso_error_count(S) > errcount) {
        return false;
      }
    }
    luaS_push_lua_module_field(L, "shuttlesock.core.module", "finish_initializing_module");
    lua_pushstring(L, module->name);
    if(!luaS_function_call_result_ok(L, 1, false)) {
      return false;
    }
  }
  for(unsigned i=0; i<S->common->modules.count; i++) {
    shuso_module_t *module = S->common->modules.array[i];
    if(!shuso_module_finalize(S, module)) {
      return false;
    }
  }
  return true;
}

static bool shuso_module_freeze(shuso_t *S, shuso_module_t *mod) {
  lua_State      *L = S->lua.state;
  if(mod == NULL) {
    return shuso_set_error(S, "can't freeze module from outside a shuttlesock module");
  }
  
  luaS_push_lua_module_field(L, "shuttlesock.core.module", "find");
  lua_pushlightuserdata(L, mod);
  if(!luaS_function_call_result_ok(L, 1, true)) {
    return false;
  }
  
  lua_getfield(L, -1, "freeze");
  lua_pushvalue(L, -2);
  if(!luaS_function_call_result_ok(L, 1, false)) {
    return false;
  }
  
  //we need parent module index maps for locating contexts, which modules might do during initialization
  lua_getfield(L, -1, "create_parent_modules_index_map");
  lua_pushvalue(L, -2);
  if(!luaS_function_call_result_ok(L, 1, true)) {
    return false;
  }
  int n = luaL_len(L, -1);
  mod->parent_modules_index_map = shuso_stalloc(&S->stalloc, sizeof(*mod->parent_modules_index_map) * n);
  if(mod->parent_modules_index_map == NULL) {
    return shuso_set_error(S, "failed to allocate parent_modules_index_map");
  }
  for(int i=0; i<n; i++) {
    lua_rawgeti(L, -1, i+1);
    int index = lua_tointeger(L, -1); //lua index
    index = index > 0 ? index - 1 : SHUSO_MODULE_INDEX_INVALID; //-1 for lua-to-c conversion
    mod->parent_modules_index_map[i]=index;
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  
  lua_getfield(L, -1, "dependent_modules");
  lua_pushvalue(L, -2);
  if(!luaS_function_call_result_ok(L, 1, true)) {
    return false;
  }
  n = luaL_len(L, -1);
  mod->submodules.count = n;
  
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  luaS_push_lua_module_field(L, "shuttlesock.core.module", "count");
  if(!luaS_function_call_result_ok(L, 0, true)) {
    return false;
  }
  
  int     total_module_count =lua_tointeger(L, -1); 
  size_t  submodule_presence_map_size = sizeof(*mod->submodules.submodule_presence_map) * total_module_count;
  lua_pop(L, 1);
  
  if((mod->submodules.submodule_presence_map = shuso_stalloc(&S->stalloc, submodule_presence_map_size)) == NULL) {
    return shuso_set_error(S, "failed to allocate memory for submodule_presence_map");
  }
  memset(mod->submodules.submodule_presence_map, '\0', submodule_presence_map_size);
#endif

  if(n == 0) {
    mod->submodules.array = NULL;
  }
  else {
    if((mod->submodules.array = shuso_stalloc(&S->stalloc, sizeof(*mod->submodules.array) * n)) == NULL) {
      return shuso_set_error(S, "failed to allocate memory for submodules array");
    }
    for(int i=1; i<=n; i++) {
      lua_rawgeti(L, -1, i);
      lua_getfield(L, -1, "ptr");
      shuso_module_t *submodule = (void *)lua_topointer(L, -1);
      mod->submodules.array[i-1]=submodule;
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
      mod->submodules.submodule_presence_map[submodule->index] = 1;
#endif
      lua_pop(L, 2);
    }
  }
  lua_pop(L, 1);
  
  lua_pop(L, 1); //pop module
  return true;
}

static bool shuso_module_finalize(shuso_t *S, shuso_module_t *mod) {
  lua_State      *L = S->lua.state;
  if(mod == NULL) {
    return shuso_set_error(S, "can't freeze module from outside a shuttlesock module");
  }
  
  luaS_push_lua_module_field(L, "shuttlesock.core.module", "find");
  lua_pushlightuserdata(L, mod);
  if(!luaS_function_call_result_ok(L, 1, true)) {
    return false;
  }
  
  lua_getfield(L, -1, "all_events_initialized");
  lua_pushvalue(L, -2);
  if(!luaS_function_call_result_ok(L, 1, false)) {
    return false;
  }
  
  lua_getfield(L, -1, "events");
  lua_getfield(L, -1, "publish");
  
  lua_pushnil(L);  /* first key */
  while (lua_next(L, -2)) {
    shuso_module_event_t *event;
    lua_getfield(L, -1, "ptr");
    event = (void *)lua_topointer(L, -1);
    assert(event != NULL);
    lua_pop(L, 1);
    
    event->module_index = mod->index;
    
    lua_getfield(L, -1, "name");
    event->name = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "data_type");
    event->data_type = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "interrupt_handler");
    if(lua_islightuserdata(L, -1)) {
      union {
        void                             *addr;
        shuso_event_interrupt_handler_fn *fn;
      } handler;
      handler.addr = (void *)lua_topointer(L, -1);
      event->interrupt_handler = handler.fn;
    }
    else {
      assert(lua_isnil(L, -1));
      event->interrupt_handler = NULL;
    }
    
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
    event->interrupt_state = SHUSO_EVENT_NO_INTERRUPT;
    event->count = 0;
    event->fired_count = 0;
#endif
    
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "listeners");
    int listeners_count = luaL_len(L, -1);
    
    shuso_module_event_listener_t *cur;
    shuso_module_event_listener_t *listeners = shuso_stalloc(&S->stalloc, sizeof(*listeners) * (listeners_count+1));
    if(listeners == NULL) {
      return shuso_set_error(S, "failed to allocate memory for event listeners");
    }
    for(int j=0; j<listeners_count; j++) {
      cur = &listeners[j];
      lua_rawgeti(L, -1, j+1);
      
      lua_getfield(L, -1, "module");
      lua_getfield(L, -1, "ptr");
      cur->module = (void *)lua_topointer(L, -1);
      lua_pop(L, 2);
      
      lua_getfield(L, -1, "listener");
      *(const void **)&cur->fn = lua_topointer(L, -1); //pointer type magic to get around "cast function to data pointer" warning
      lua_pop(L, 1);
      
      lua_getfield(L, -1, "privdata");
      cur->pd = (void *)lua_topointer(L, -1);
      lua_pop(L, 1);
      
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
      lua_getfield(L, -1, "priority");
      cur->priority = lua_tointeger(L, -1);
      lua_pop(L, 1);
#endif
      
      lua_pop(L, 1);
      
      luaS_push_lua_module_field(L, "shuttlesock.core.module", "dependency_index");
      lua_pushstring(L, mod->name);
      lua_pushstring(L, cur->module->name);
      if(!luaS_function_call_result_ok(L, 2, true)) {
        return false;
      }
      lua_pop(L, 1);
    }
    
    listeners[listeners_count] = (shuso_module_event_listener_t ) {
      //end-of-list sentinel
      .module = NULL,
      .fn = NULL
    };
    event->listeners = listeners;
    lua_pop(L, 2);
  }
  lua_pop(L, 2);
  
  lua_pop(L, 1); //pop module
  return true;
}

bool shuso_context_list_initialize(shuso_t *S, shuso_module_t *parent, shuso_module_context_list_t *context_list, shuso_stalloc_t *stalloc) {
  if(parent == NULL) {
    parent = &shuso_core_module;
  }
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  context_list->parent = parent;
#endif
  int n = parent->submodules.count;
  if(n > 0) {
    context_list->context = shuso_stalloc(stalloc, sizeof(void *) * n);
    if(!context_list->context) {
      return shuso_set_error(S, "failed to allocate context_list");
    }
  }
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  else {
    context_list->context = NULL;
  }
#endif
  return true;
}

#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
#define check_context_offset(S, parent, module, context_list, offset) \
  do { \
    if(offset == SHUSO_MODULE_INDEX_INVALID) { \
      shuso_set_error(S, "No context for module %s in %s", module->name, parent->name); \
      raise(SIGABRT); \
      return NULL; \
    } \
    assert(offset > 0 && offset < context_list->parent->submodules.count); \
  } while(0)
#else
#define check_context_offset(S, parent, module, context_list, offset) \
  do { \
    if(offset == SHUSO_MODULE_INDEX_INVALID) { \
      shuso_set_error(S, "No context for module %s in %s", module->name, parent->name); \
      return NULL; \
    } \
  } while(0)
#endif

void *shuso_context(shuso_t *S, shuso_module_t *parent, shuso_module_t *module, shuso_module_context_list_t *context_list) {
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  assert(context_list->parent == parent);
  assert(parent->submodules.submodule_presence_map[module->index] == 1);
#endif
  int offset = module->parent_modules_index_map[parent->index];
  check_context_offset(S, parent, module, context_list, offset);
  return context_list->context[offset];
}

bool shuso_set_context(shuso_t *S, shuso_module_t *parent, shuso_module_t *module, void *ctx, shuso_module_context_list_t *context_list) {
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  assert(context_list->parent == parent);
  assert(parent->submodules.submodule_presence_map[module->index] == 1);
#endif
  int offset = module->parent_modules_index_map[parent->index];
  check_context_offset(S, parent, module, context_list, offset);
  context_list->context[offset] = ctx;
  return true;
}

shuso_module_t *shuso_get_module_by_name(shuso_t *S, const char *name) {
  lua_State         *L = S->lua.state;
  shuso_module_t    *module;
  luaS_push_lua_module_field(L, "shuttlesock.core.module", "find");
  lua_pushstring(L, name);
  if(!luaS_function_call_result_ok(L, 1, true)) {
    return NULL;
  }
  lua_getfield(L, -1, "ptr");
  module = (void *)lua_topointer(L, -1);
  if(module == NULL) {
    shuso_set_error(S, "module pointer not found");
    return NULL;
  }
  return module;
}

shuso_module_t *shuso_get_module_by_index(shuso_t *S, int index) {
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  assert(index>=0);
  assert(index < S->common->modules.count);
#endif
  return S->common->modules.array[index];
}

