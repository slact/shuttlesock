#include <shuttlesock.h>

void *shuso_events(shuso_t *S, shuso_module_t *module) {
  return S->common->modules.events[module->index];
}

bool shuso_event_initialize(shuso_t *S, shuso_module_t *mod, shuso_module_event_t *mev, shuso_event_init_t *event_init) {
  lua_State       *L = S->lua.state;
  if(mod == NULL) {
    return shuso_set_error(S, "can't initialize event from outside a shuttlesock module");
  }
  luaS_push_lua_module_field(L, "shuttlesock.core.module", "initialize_event");
  lua_pushlightuserdata(L, mod);
  
  lua_newtable(L);
  lua_pushstring(L, event_init->name);
  lua_setfield(L, -2, "name");
  
  lua_pushlightuserdata(L, mev);
  lua_setfield(L, -2, "ptr");
  
  if(event_init->data_type) {
    lua_pushstring(L, event_init->data_type);
    lua_setfield(L, -2, "data_type");
  }
  
  lua_pushboolean(L, event_init->cancelable);
  lua_setfield(L, -2, "cancelable");
  
  lua_pushboolean(L, event_init->pausable);
  lua_setfield(L, -2, "pausable");
  
  return luaS_function_call_result_ok(L, 2, false);
}

bool shuso_events_initialize(shuso_t *S, shuso_module_t *module, shuso_event_init_t *event_init) {
  for(shuso_event_init_t *cur = event_init; cur && cur->name && cur->event; cur++) {
    if(!shuso_event_initialize(S, module, cur->event, cur)) {
      return false;
    }
  }
  return true;
}

bool shuso_event_listen(shuso_t *S, const char *name, shuso_module_event_fn *callback, void *pd) {
  return shuso_event_listen_with_priority(S, name, callback, pd, 0);
}

bool shuso_event_listen_with_priority(shuso_t *S, const char *name, shuso_module_event_fn *callback, void *pd, int8_t priority) {
  lua_State       *L = S->lua.state;
  shuso_module_t  *module;
  int              top = lua_gettop(L);
  
  bool optional = name[0] == '~';
  if(optional) {
    name = &name[1];
  }
  
  luaS_push_lua_module_field(L, "shuttlesock.core.module", "currently_initializing_module");
  if(!luaS_function_call_result_ok(L, 0, true)) {
    return false;
  }
  lua_getfield(L, -1, "ptr");
  
  module = (void *)lua_topointer(L, -1);
  lua_pop(L, 2);
  
  luaS_push_lua_module_field(L, "shuttlesock.core.module_event", "find");
  lua_pushstring(L, name);
  luaS_call(L, 1, 2);
  if(lua_isnil(L, -2)) {
    //module not found
    if(optional) {
      lua_settop(L, top);
      return true;
    }
    else {
      shuso_set_error(S, "%s", lua_isnil(L, -1) ? "failed to find module" : lua_tostring(L, -1));
      lua_settop(L, top);
      return false;
    }
  }
  lua_pop(L, 1);
  
  lua_getfield(L, -1, "add_listener");
  lua_pushvalue(L, -2);
  lua_pushlightuserdata(L, module);
  lua_pushlightuserdata(L, *((void **)&callback));
  lua_pushlightuserdata(L, pd);
  lua_pushnumber(L, priority);
    
  luaS_call(L, 5, 2);
  if(lua_isnil(L, -2)) {
    if(optional) {
      lua_settop(L, top);
      return true;
    }
    else {
      shuso_set_error(S, "%s", lua_isnil(L, -1) ? "failed to add listener" : lua_tostring(L, -1));
      lua_settop(L, top);
      return false;
    }
  }
  lua_settop(L, top);
  return true;
}

typedef struct {
  shuso_module_event_t  *event;
  shuso_event_state_t    state;
  int                    current_listener_index;
  intptr_t               code;
  void                  *data;
  shuso_module_event_listener_t *cur;
  bool                   stopped;
} complete_evstate_t;

static bool fire_event(shuso_t *S, shuso_module_event_t *event, int listener_start_index, intptr_t code, void *data) {
  complete_evstate_t cev = {
    .event = event,
    .state = {
      .publisher = shuso_get_module(S, event->module_index),
      .name = event->name,
      .data_type = event->data_type,
    },
    .stopped = false
  };
  
  bool pausable = event->pausable;
  
  if(!event->cancelable && !event->pausable) {
    for(cev.cur = &event->listeners[listener_start_index]; cev.cur->fn != NULL; cev.cur++) {
      cev.state.module = cev.cur->module;
      cev.cur->fn(S, &cev.state, code, data, cev.cur->pd);
    }
  }
  else {
    if(pausable) {
      cev.code = code;
      cev.data = data;
    }
    for(cev.cur = &event->listeners[listener_start_index]; !cev.stopped && cev.cur->fn != NULL; cev.cur++) {
      cev.state.module = cev.cur->module;
      cev.cur->fn(S, &cev.state, code, data, cev.cur->pd);
    }
  }
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  if(!resume) {
    event->fired_count++;
  }
#endif
  return !cev.stopped;
}

bool shuso_event_publish(shuso_t *S, shuso_module_event_t *event, intptr_t code, void *data) {
  return fire_event(S, event, 0, code, data);
}

bool shuso_event_cancel(shuso_t *S, shuso_event_state_t *evstate) {
  complete_evstate_t *cev = container_of(evstate, complete_evstate_t, state);
  if(!cev->event->cancelable) {
    shuso_set_error(S, "event %s:%s cannot be canceled", cev->state.publisher->name, cev->state);
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
    raise(SIGABRT);
#endif
    return false;
  }
  cev->stopped = true;
  return true;
}

bool shuso_event_pause(shuso_t *S, shuso_event_state_t *evstate, shuso_module_paused_event_t *paused) {
  complete_evstate_t *cev = container_of(evstate, complete_evstate_t, state);
  if(!cev->event->pausable) {
    shuso_set_error(S, "event %s:%s cannot be canceled", cev->state.publisher->name, cev->state);
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
    raise(SIGABRT);
#endif
    return false;
  }
  cev->stopped = true;
  
  *paused = (shuso_module_paused_event_t) {
    .event = cev->event,
    .code = cev->code,
    .data =cev->data,
    .next_listener_index = cev->cur - &cev->event->listeners[0] + 1
  };
  
  return true;
}
bool shuso_event_resume(shuso_t *S, shuso_module_paused_event_t *paused) {
  return fire_event(S, paused->event, paused->next_listener_index, paused->code, paused->data);
}

/*
bool shuso_register_event_data_type_mapping(shuso_t *S, shuso_event_data_type_map_t *t, shuso_module_t *registering_module, bool replace_if_present) {
  lua_State *L = S->lua.state;
  
  //does it already exist?
  luaS_push_lua_module_field(L, "shuttlesock.core.module_event", "data_type_map");
  lua_pushstring(L, t->language);
  lua_pushstring(L, t->data_type);
  luaS_call(L, 2, 2); //returns data_type_map_ptr, module_name
  
  if(!lua_isnil(L, -2)) {
    //yeah, it exists
    if(replace_if_present) {
      //but that's okay
      luaS_push_lua_module_field(L, "shuttlesock.core.module_event", "unregister_data_type");
      lua_pushstring(L, t->language);
      lua_pushstring(L, t->data_type);
      luaS_function_call_result_ok(L, 2, false);
    }
  }
  lua_pop(L, 2);
  
  shuso_event_data_type_map_t *map = shuso_stalloc(&S->stalloc, sizeof(*map));
  if(!map) {
    return shuso_set_error(S, "failed to allocate map while registering event data type");
  }
  *map = *t;
  map->language = shuso_stalloc(&S->stalloc, strlen(t->language)+1);
  map->data_type = shuso_stalloc(&S->stalloc, strlen(t->data_type)+1);
  if(!map->language || !map->data_type) {
    return shuso_set_error(S, "failed to allocate map while registering event data type");
  }
  strcpy((char *)map->language, t->language);
  strcpy((char *)map->data_type, t->data_type);
  
  luaS_push_lua_module_field(L, "shuttlesock.core.module_event", "register_data_type");
  lua_pushstring(L, map->language);
  lua_pushstring(L, map->data_type);
  lua_pushstring(L, registering_module->name);
  lua_pushlightuserdata(L, map);
  
  return luaS_function_call_result_ok(L, 4, false);
}
*/
