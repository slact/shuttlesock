#include <shuttlesock.h>

static int luaS_find_module_table(lua_State *L, const char *name) {
  lua_getglobal(L, "require");
  lua_pushliteral(L, "shuttlesock.module");
  lua_call(L, 1, 1);
  lua_getfield(L, -1, "find");
  lua_remove(L, -2);
  lua_pushstring(L, name);
  lua_call(L, 1, 1);
  return 1;
}

/*
static bool luaS_module_pointer_ref(lua_State *L, const void *ptr) {
  luaS_pointer_ref(L, "shuttlesock.lua_module_bridge.pointer_ref_table", ptr);
  return true;
}

static bool luaS_get_module_pointer_ref(lua_State *L, const void *ptr) {
  luaS_get_pointer_ref(L, "shuttlesock.lua_module_bridge.pointer_ref_table", ptr);
  return lua_isnil(L, -1);
}
*/
static bool lua_module_initialize_config(shuso_t *S, shuso_module_t *module, shuso_setting_block_t *block) {
  return true;
}

static bool lua_module_initialize_events(shuso_t *S, shuso_module_t *module) {
  lua_State *L = S->lua.state;
  luaS_find_module_table(L, module->name);
  
  lua_getfield(L, -1, "events");
  lua_getfield(L, -1, "publish");
  
  int npub = luaS_table_count(L, -1);
  if(npub > 0) {
    shuso_module_event_t *events = shuso_stalloc(&S->stalloc, sizeof(*events) * npub);
    shuso_event_init_t *events_init = malloc(sizeof(*events_init) * (npub + 1));
    if(events == NULL || events_init == NULL) {
      return shuso_set_error(S, "failed to allocate lua module published events array");
    }
    lua_pushnil(L);
    for(int i=0; lua_next(L, -2) != 0; i++) {
      assert(i<npub);
      lua_pop(L, 1);
      events_init[i]=(shuso_event_init_t ){
        .name = lua_tostring(L, -1),
        .event = &events[i]
      };
      
    }
    events_init[npub]=(shuso_event_init_t ){.name = NULL, .event = NULL};
    if(!shuso_events_initialize(S, module, events, events_init)) {
      free(events_init);
      return false;
    }
    free(events_init);
  }
  lua_pop(L, 1);
  
  lua_getfield(L, -1, "subscribe");
  
  
  return true;
}



bool shuso_add_lua_module(shuso_t *S, int pos) {
  lua_State *L = S->lua.state;
  
  luaL_checktype(L, pos, LUA_TTABLE);
  shuso_module_t *m = shuso_stalloc(&S->stalloc, sizeof(*m));
  if(m == NULL) {
    return shuso_set_error(S, "failed to allocate lua module struct");
  }
  memset(m, '\0', sizeof(*m));
  //shuso_lua_module_data_t *d = shuso_stalloc(&S->stalloc, sizeof(*d));
  //m->privdata = d;
  
  lua_getfield(L, pos, "name");
  m->name = lua_tostring(L, -1);
  lua_pop(L, 1);
  
  lua_getfield(L, pos, "version");
  m->version = lua_tostring(L, -1);
  lua_pop(L, 1);
  
  lua_getfield(L, pos, "parent_modules");
  m->parent_modules = lua_tostring(L, -1);
  lua_pop(L, 1);
  
  lua_getfield(L, pos, "subscribe");
  if(lua_istable(L, -1)) {
    luaS_table_concat(L, " ");
  }
  m->subscribe = lua_tostring(L, -1);
  lua_pop(L, 1);
  
  lua_getfield(L, pos, "publish");
  if(lua_istable(L, -1)) {
    luaS_table_concat(L, " ");
  }
  m->publish = lua_tostring(L, -1);
  lua_pop(L, 1);
  
  m->initialize_config = lua_module_initialize_config;
  m->initialize_events = lua_module_initialize_events;
  
  if(!shuso_add_module(S, m)) {
    return false;
  }
  
  lua_pop(L, 1);
  return true;
}


static void lua_module_gxcopy(shuso_t *S, shuso_event_state_t *es, intptr_t code, void *data, void *pd) {
  shuso_t *Sm = data;
  lua_State *L = S->lua.state;
  lua_State *Lm = Sm->lua.state;
  
  luaS_gxcopy_module_state(Lm, L, "shuttlesock.lua_module");
}

static bool lua_bridge_module_init_events(shuso_t *S, shuso_module_t *self) {
  shuso_event_listen(S, "core:worker.start.before.lua_gxcopy", lua_module_gxcopy, self);
  
  return true;
}

shuso_module_t shuso_lua_bridge_module = {
  .name = "lua_bridge",
  .version = "0.0.1",
  .subscribe = 
   " core:worker.start.before.lua_gxcopy",
  .initialize_events = lua_bridge_module_init_events
};
