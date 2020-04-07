#include <shuttlesock.h>
#include <shuttlesock/internal.h>

void *shuso_events(shuso_t *S, shuso_module_t *module) {
  return S->common->modules.events[module->index];
}

bool shuso_event_initialize(shuso_t *S, shuso_module_t *mod, shuso_event_t *mev, shuso_event_init_t *event_init) {
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
  
  if(event_init->interrupt_handler) {
    union {
      void                              *addr;
      shuso_event_interrupt_handler_fn  *fn;
    } handler;
    handler.fn = event_init->interrupt_handler;
    lua_pushlightuserdata(L, handler.addr);
    lua_setfield(L, -2, "interrupt_handler");
  }
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

bool shuso_event_listen(shuso_t *S, const char *name, shuso_event_fn *callback, void *pd) {
  return shuso_event_listen_with_priority(S, name, callback, pd, 0);
}

bool shuso_event_listen_with_priority(shuso_t *S, const char *name, shuso_event_fn *callback, void *pd, int8_t priority) {
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
  
  luaS_push_lua_module_field(L, "shuttlesock.core.event", "find");
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
  shuso_event_t             *event;
  shuso_event_state_t        state;
  int                        current_listener_index;
  intptr_t                   code;
  void                      *data;
  shuso_event_listener_t    *cur;
  shuso_event_interrupt_t    interrupt;
  const char                *interrupt_reason;
} shuso_complete_event_state_t;

static bool fire_event(shuso_t *S, shuso_event_t *event, int listener_start_index, intptr_t code, void *data) {
  shuso_module_t *publisher = shuso_get_module(S, event->module_index);
  shuso_complete_event_state_t cev = {
    .event = event,
    .state = {
      .publisher = publisher,
      .name = event->name,
      .data_type = event->data_type,
    },
    .interrupt = SHUSO_EVENT_NO_INTERRUPT,
    .interrupt_reason = NULL
  };
  
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  if(expected_interrupt_state != event->interrupt_state) {
    const char *interrupt_state_string;
    switch(event->interrupt_state) {
      case SHUSO_EVENT_NO_INTERRUPT:
        interrupt_state_string = "uninterrupted";
        break;
      case SHUSO_EVENT_PAUSE:
        interrupt_state_string = "paused";
        break;
      case SHUSO_EVENT_CANCEL:
        interrupt_state_string = "canceled";
        break;
      case SHUSO_EVENT_DELAY:
        interrupt_state_string = "delayed";
        break;
    }
    
    switch(expected_interrupt_state) {
      case SHUSO_EVENT_NO_INTERRUPT:
        return shuso_set_error(S, "cannot publish %s event", interrupt_state_string);
      case SHUSO_EVENT_PAUSE:
        return shuso_set_error(S, "cannot unpause %s event", interrupt_state_string);
      case SHUSO_EVENT_DELAY:
        return shuso_set_error(S, "cannot undelay %s event", interrupt_state_string);
      case SHUSO_EVENT_CANCEL:
        return shuso_set_error(S, "cannot uncancel event -- that's not even possible");
    }
  }
  if(listener_start_index == 0) {
    event->fired_count++;
  }
#endif
  shuso_log_debug(S, "event %s:%s started", publisher->name, event->name);
  if(event->interrupt_handler == NULL) {
    for(cev.cur = &event->listeners[listener_start_index]; cev.cur->fn != NULL; cev.cur++) {
      cev.state.module = cev.cur->module;
      cev.cur->fn(S, &cev.state, code, data, cev.cur->pd);
    }
  }
  else {
    cev.code = code;
    cev.data = data;
    for(cev.cur = &event->listeners[listener_start_index]; cev.interrupt == SHUSO_EVENT_NO_INTERRUPT && cev.cur->fn != NULL; cev.cur++) {
      cev.state.module = cev.cur->module;
      cev.cur->fn(S, &cev.state, code, data, cev.cur->pd);
    }
  }
  
  const char *interrupt_action;
  
  switch(cev.interrupt) {
    case SHUSO_EVENT_NO_INTERRUPT:
      interrupt_action = "uninterrupted";
      shuso_log_debug(S, "event %s:%s finished", publisher->name, event->name);
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
      event->interrupt_state = SHUSO_EVENT_NO_INTERRUPT;
#endif
      return true;
    case SHUSO_EVENT_PAUSE:
      interrupt_action = "paused";
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
      event->interrupt_state = SHUSO_EVENT_PAUSE;
#endif
      break;
    case SHUSO_EVENT_CANCEL:
      interrupt_action = "canceled";
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
      event->interrupt_state = SHUSO_EVENT_NO_INTERRUPT;
#endif
      break;
    case SHUSO_EVENT_DELAY:
      interrupt_action = "delayed";
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
      event->interrupt_state = SHUSO_EVENT_DELAY;
#endif
      break;
    default:
      interrupt_action = "<?>";
      break;
  }
  
  if(cev.interrupt_reason) {
    shuso_log_info(S, "event %s:%s %s by module %s: %s", cev.state.publisher->name, cev.event->name, interrupt_action, cev.state.module->name, cev.interrupt_reason);
  }
  else {
    shuso_log_debug(S, "event %s:%s %s by module %s", cev.state.publisher->name, cev.event->name, interrupt_action, cev.state.module->name);
  }
  return false;
}

bool shuso_event_publish(shuso_t *S, shuso_event_t *event, intptr_t code, void *data) {
  return fire_event(S, event, 0, code, data);
}


static bool try_to_interrupt_event(shuso_t *S, shuso_complete_event_state_t *cev, const char *what, shuso_event_interrupt_t interrupt, double *sec) {
  shuso_event_interrupt_handler_fn *interrupt_handler = cev->event->interrupt_handler;
  
  if(interrupt_handler == NULL) {
    raise(SIGABRT);
    shuso_set_error(S, "event %s:%s cannot be %s by module %s", cev->state.publisher->name, cev->event->name, what, cev->state.module->name);
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
    raise(SIGABRT);
#endif
    return false;
  }
  
  if(!interrupt_handler(S, cev->event, &cev->state, interrupt, sec)) {
    return false;
  }
  return true;
}

bool shuso_event_cancel(shuso_t *S, shuso_event_state_t *evstate) {
  shuso_complete_event_state_t *cev = container_of(evstate, shuso_complete_event_state_t, state);
  
  if(!try_to_interrupt_event(S, cev, "canceled", SHUSO_EVENT_CANCEL, NULL)) {
    return false;
  }
  
  cev->interrupt = SHUSO_EVENT_CANCEL;
  return true;
}

bool shuso_event_pause(shuso_t *S, shuso_event_state_t *evstate, const char *reason, shuso_event_pause_t *paused) {
  shuso_complete_event_state_t *cev = container_of(evstate, shuso_complete_event_state_t, state);
  
  if(!try_to_interrupt_event(S, cev, "paused", SHUSO_EVENT_PAUSE, NULL)) {
    return false;
  }
  
  cev->interrupt = SHUSO_EVENT_PAUSE;
  
  *paused = (shuso_event_pause_t) {
    .reason = reason,
    .event = cev->event,
    .code = cev->code,
    .data =cev->data,
    .next_listener_index = cev->cur - &cev->event->listeners[0] + 1
  };
  
  return true;
}

static void delayed_event_handler(shuso_loop *, shuso_ev_timer *, int);

bool shuso_event_delay(shuso_t *S, shuso_event_state_t *evstate, const char *reason, double max_delay_sec, int *delay_ref) {
  shuso_complete_event_state_t *cev = container_of(evstate, shuso_complete_event_state_t, state);
  
  if(!try_to_interrupt_event(S, cev, "delayed", SHUSO_EVENT_DELAY, &max_delay_sec)) {
    return false;
  }
  
  if(max_delay_sec <= 0) {
    return false;
  }
  
  cev->interrupt = SHUSO_EVENT_DELAY;
  
  lua_State  *L = S->lua.state;
  int         top = lua_gettop(L);
  
  luaS_push_lua_module_field(L, "shuttlesock.core.event", "find");
  lua_pushlightuserdata(L, cev->event);
  luaS_call(L, 1, 1);
  if(lua_isnil(L, -1)) {
    lua_settop(L, top);
    shuso_set_error(S, "event %s:%s cannot be delayed", cev->state.publisher->name, cev->state);
    return false;
  }
  
  shuso_event_delay_t *delayed = lua_newuserdata(L, sizeof(*delayed));
  luaL_newmetatable(L, "shuttlesock.core.event.delayed");
  lua_setmetatable(L, -2);
  
  if(!delayed) {
    lua_settop(L, top);
    shuso_set_error(S, "event %s:%s cannot be delayed due to a failed allocation", cev->state.publisher->name, cev->state);
    return false;
  }
  *delayed = (shuso_event_delay_t ){
    .paused.reason = reason,
    .paused.event = cev->event,
    .paused.code = cev->code,
    .paused.data = cev->data,
    .paused.next_listener_index = cev->cur - &cev->event->listeners[0]
  };
  delayed->ref = luaL_ref(L, LUA_REGISTRYINDEX);
  if(delay_ref) {
    *delay_ref = delayed->ref;
  }
  
  shuso_ev_init(S, &delayed->timer, max_delay_sec, 0.0, delayed_event_handler, delayed);
  shuso_ev_start(S, &delayed->timer);
  
  lua_settop(L, top);
  return true;
}

static bool clear_delayed_state(shuso_t *S, shuso_event_delay_t *delay, bool error_out, shuso_event_pause_t *pause) {
  lua_State  *L = S->lua.state;
  int         top = lua_gettop(L);
  
  luaS_push_lua_module_field(L, "shuttlesock.core.event", "find");
  lua_pushlightuserdata(L, delay->paused.event);
  luaS_call(L, 1, 1);
  if(lua_isnil(L, -1)) {
    lua_settop(L, top);
    if(error_out) {
      shuso_set_error(S, "delayed_event_handler couldn't find the event");
    }
    return false;
  }
  
  if(pause != NULL) {
    *pause = delay->paused;
  }
  luaL_unref(L, LUA_REGISTRYINDEX, delay->ref);
  return true;
}

static void delayed_event_handler(shuso_loop *loop, shuso_ev_timer *w, int event) {
  shuso_t    *S = shuso_state(loop, w);
  shuso_event_delay_t      *delay = shuso_ev_data(w);
  shuso_event_pause_t       pause;
  
  if(!clear_delayed_state(S, delay, true, &pause)) {
    return;
  }
  
  if(!(event & EV_TIMER)) {
    return;
  }
  
  fire_event(S, pause.event, pause.next_listener_index, pause.code, pause.data);
}

bool shuso_event_resume_delayed(shuso_t *S, int delay_id) {
  lua_State *L = S->lua.state;
  lua_rawgeti(L, LUA_REGISTRYINDEX, delay_id);
  if(!lua_isuserdata(L, -1)) {
    shuso_set_error(S, "failed to resume delayed event: invalid delay id");
  }
  
  shuso_event_delay_t *delay = (void *)lua_topointer(L, -1);
  lua_pop(L, 1);
  
  shuso_event_pause_t  pause;
  
  if(!clear_delayed_state(S, delay, false, &pause)) {
    return false;
  }
  
  return fire_event(S, pause.event, pause.next_listener_index, pause.code, pause.data);
}

bool shuso_event_resume_paused(shuso_t *S, shuso_event_pause_t *pause) {
  return fire_event(S, pause->event, pause->next_listener_index, pause->code, pause->data);
}
