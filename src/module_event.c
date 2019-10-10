#include <shuttlesock.h>

void *shuso_events(shuso_t *S, shuso_module_t *module) {
  return S->common->modules.events[module->index];
}

static bool shuso_event_initialize(shuso_t *S, shuso_module_t *mod, const char *name, shuso_module_event_t *mev, const char *data_type) {
  lua_State       *L = S->lua.state;
  if(mod == NULL) {
    return shuso_set_error(S, "can't initialize event from outside a shuttlesock module");
  }
  luaS_push_module_function(L, "initialize_event");
  lua_pushlightuserdata(L, mod);
  lua_pushstring(L, name);
  lua_pushlightuserdata(L, mev);
  if(data_type) {
    lua_pushstring(L, data_type);
  }
  else {
    lua_pushnil(L);
  }
  return luaS_function_call_result_ok(L, 4, false);
}

bool shuso_events_initialize(shuso_t *S, shuso_module_t *module,  void *events_struct, shuso_event_init_t *event_init) {
  for(shuso_event_init_t *cur = event_init; cur && cur->name && cur->event; cur++) {
    if(!shuso_event_initialize(S, module, cur->name, cur->event, cur->data_type)) {
      return false;
    }
  }
  S->common->modules.events[module->index] = events_struct;
  return true;
}

bool shuso_event_listen(shuso_t *S, const char *name, shuso_module_event_fn *callback, void *pd) {
  lua_State       *L = S->lua.state;
  shuso_module_t  *module;
  
  luaS_push_module_function(L, "currently_initializing_module");
  if(!luaS_function_call_result_ok(L, 0, true)) {
    return false;
  }
  lua_getfield(L, -1, "ptr");
  
  module = (void *)lua_topointer(L, -1);
  lua_pop(L, 2);
  
  luaS_push_module_function(L, "find_event");
  lua_pushstring(L, name);
  if(!luaS_function_call_result_ok(L, 1, true)) {
    return false;
  }
  
  lua_getfield(L, -1, "add_listener");
  lua_pushvalue(L, -2);
  lua_pushlightuserdata(L, module);
  lua_pushlightuserdata(L, *((void **)&callback));
  lua_pushlightuserdata(L, pd);
  if(!luaS_function_call_result_ok(L, 4, false)) {
    lua_pop(L, 1);
    return false;
  }
  lua_pop(L, 1);
  return true;
  
}

bool shuso_event_publish(shuso_t *S, shuso_module_t *publisher_module, shuso_module_event_t *event, intptr_t code, void *data) {
  shuso_event_state_t evstate = {
    .publisher = publisher_module,
    .name = event->name,
    .data_type = event->data_type
  };
  for(shuso_module_event_listener_t *cur = &event->listeners[0]; cur->fn != NULL; cur++) {
    cur->fn(S, &evstate, code, data, cur->pd);
  }
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  event->fired_count++;
#endif
  return true;
}

bool shuso_context_list_initialize(shuso_t *S, shuso_module_t *parent, shuso_module_context_list_t *context_list, shuso_stalloc_t *stalloc) {
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  context_list->parent = parent;
#endif
  int n = parent->submodules.count;
  context_list->context = shuso_stalloc(stalloc, sizeof(void *) * n);
  if(!context_list->context) {
    return shuso_set_error(S, "failed to allocate context_list");
  }
  return true;
}
