#include <shuttlesock.h>
#include <shuttlesock/embedded_lua_scripts.h>
#include <lauxlib.h>

static bool shuso_module_freeze(shuso_t *S, shuso_module_t *mod);
static bool shuso_module_finalize(shuso_t *S, shuso_module_t *mod);

bool shuso_module_system_initialize(shuso_t *S, shuso_module_t *core_module) {
  return shuso_set_core_module(S, core_module);
}

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

bool shuso_set_core_module(shuso_t *S, shuso_module_t *module) {
  if(!add_module(S, module, "new_core_module")) {
    return false;
  }
  assert(module->index == 0);
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
    mod->parent_modules_index_map[i]=lua_tointeger(L, -1) - 1; //-1 for lua-to-C index conversion
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
  if(!luaS_function_call_result_ok(L, 1, true)) {
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
    
    lua_getfield(L, -1, "name");
    event->name = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "data_type");
    event->data_type = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "cancelable");
    event->cancelable = lua_toboolean(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "listeners");
    int listeners_count = luaL_len(L, -1);
    
    shuso_module_event_listener_t *cur;
    shuso_module_event_listener_t *listeners = shuso_stalloc(&S->stalloc, sizeof(*listeners) * (mod->submodules.count+1));
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

void *shuso_context(shuso_t *S, shuso_module_t *parent, shuso_module_t *module, shuso_module_context_list_t *context_list) {
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  assert(context_list->parent == parent);
  assert(parent->submodules.submodule_presence_map[module->index] == 1);
#endif
  int offset = module->parent_modules_index_map[parent->index];
  return context_list->context[offset];
}

bool shuso_set_context(shuso_t *S, shuso_module_t *parent, shuso_module_t *module, void *ctx, shuso_module_context_list_t *context_list) {
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  assert(context_list->parent == parent);
  assert(parent->submodules.submodule_presence_map[module->index] == 1);
#endif
  int offset = module->parent_modules_index_map[parent->index];
  context_list->context[offset] = ctx;
  return true;
}

bool shuso_set_core_context(shuso_t *S, shuso_module_t *module, void *ctx) {
  return shuso_set_context(S, &shuso_core_module, module, ctx, &S->common->module_ctx.core->context_list);
}

void *shuso_core_context(shuso_t *S, shuso_module_t *module) {
  return shuso_context(S, &shuso_core_module, module, &S->common->module_ctx.core->context_list);
}

shuso_module_t *shuso_get_module(shuso_t *S, const char *name) {
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

bool shuso_core_module_event_publish(shuso_t *S, const char *name, intptr_t code, void *data) {
  shuso_core_module_events_t *events = shuso_events(S, &shuso_core_module);
  shuso_module_event_t    *ev = (shuso_module_event_t *)events;
  shuso_module_event_t    *cur;
  int n = sizeof(*events)/sizeof(shuso_module_event_t);
  for(int i = 0; i < n; i++) {
    cur = &ev[i];
    if(strcmp(cur->name, name) == 0) {
      return shuso_event_publish(S, &shuso_core_module, cur, code, data);
    }
  }
  return shuso_set_error(S, "failed to publish core event %s: no such event", name);
}

static void core_gxcopy(shuso_t *S, shuso_event_state_t *evs, intptr_t status, void *data, void *pd) {
  //do nothing, all modules are copied over by the lua_bridge_module
}

static bool core_module_initialize(shuso_t *S, shuso_module_t *self) {
  shuso_core_module_events_t *events = shuso_stalloc(&S->stalloc, sizeof(*events));
  shuso_events_initialize(S, self, events, (shuso_event_init_t[]){
    {"configure",       &events->configure,         NULL, false},
    {"configure.after", &events->configure_after,   NULL, false},
    
    {"master.start",    &events->start_master,      NULL, false},
    {"manager.start",   &events->start_manager,     NULL, false},
    {"worker.start",    &events->start_worker,      NULL, false},
    {"worker.start.before.lua_gxcopy",&events->start_worker_before_lua_gxcopy, "shuttlesock_state", false},
    {"worker.start.before",&events->start_worker_before, "shuttlesock_state", false},
    
    {"master.stop",     &events->stop_master,       NULL, false},
    {"manager.stop",    &events->stop_manager,      NULL, false},
    {"worker.stop",     &events->stop_worker,       NULL, false},
    
    {"manager.workers_started",   &events->manager_all_workers_started, NULL, false},
    {"master.workers_started",    &events->master_all_workers_started,  NULL, false},
    {"worker.workers_started",    &events->worker_all_workers_started,  NULL, false},
    {"manager.worker_exited",     &events->worker_exited,               NULL, false},
    {"master.manager_exited",     &events->manager_exited,              NULL, false},
    
    {"error",                     &events->error,              "string", false},
    {.name=NULL}
  });
  
  shuso_event_listen(S, "core:worker.start.before.lua_gxcopy", core_gxcopy, self);
  
  shuso_core_module_ctx_t *ctx = shuso_stalloc(&S->stalloc, sizeof(*ctx));
  assert(ctx);
  shuso_context_list_initialize(S, self, &ctx->context_list, &S->stalloc);
  
  S->common->module_ctx.core = ctx;
  
  return true;
}
shuso_module_t shuso_core_module = {
  .name = "core",
  .version = "0.0.1",
  .publish = 
   " configure"
   " configure.after"
   
   " master.start"
   " manager.start"
   " worker.start.before" //worker state created, but pthread not yet started
   " worker.start.before.lua_gxcopy" //worker state created, but pthread not yet started, luaS_gxcopy started
   " worker.start"
   
   " master.stop"
   " manager.stop"
   " worker.stop"
   
   " manager.workers_started"
   " master.workers_started"
   " worker.workers_started"
   " manager.worker_exited"
   " master.manager_exited"
   
   " error"
  ,
  .subscribe = 
   " core:worker.start.before.lua_gxcopy"
  ,
  .initialize = core_module_initialize
};
