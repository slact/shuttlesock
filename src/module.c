#include <shuttlesock.h>
#include <shuttlesock/embedded_lua_scripts.h>
#include <lauxlib.h>

static bool lua_function_call_result_ok(shuso_t *S, lua_State *L, int nargs, bool preserve_result) {
  lua_call(L, nargs, 2);
  if(lua_isnil(L, -2)) {
    if(!lua_isstring(L, -1)) {
      shuso_set_error(S, "lua function returned nil with no error message");      
    }
    else {
      shuso_set_error(S, "%s", lua_tostring(L, -1));
    }
    lua_pop(L, 2);
    return false;
  }
  bool ret = lua_toboolean(L, -2);
  if(preserve_result) {
    lua_pop(L, 1); //just pop the nil standin for the error
  }
  else {
    lua_pop(L, 2);
  }
  return ret;
}

static bool Lua_push_module_function(lua_State *L, const char *funcname) {
  lua_getfield(L, LUA_REGISTRYINDEX, "shuttlesock.modules");
  lua_getfield(L, -1, funcname);
  lua_remove(L, -2);
  return true;
}

bool shuso_module_system_initialize(shuso_t *S) {
  lua_State *L = S->lua.state;;
  shuso_Lua_do_embedded_script(L);
  lua_setfield(L, LUA_REGISTRYINDEX, "shuttlesock.modules");
  
  return true;
}

bool shuso_add_module(shuso_t *S, shuso_module_t *module) {
  lua_State *L = S->lua.state;
  Lua_push_module_function(L, "new");
  lua_pushlightuserdata(L, module);
  lua_pushstring(L, module->name);
  lua_pushstring(L, module->version);
  lua_pushstring(L, module->subscribe);
  lua_pushstring(L, module->publish); 
  lua_pushstring(L, module->parent_modules); 
  return lua_function_call_result_ok(S, L, 5, false);
}

bool shuso_module_event_initialize(shuso_t *S, const char *name, shuso_module_t *mod, shuso_module_event_t *mev) {
  lua_State       *L = S->lua.state;
  if(mod == NULL) {
    return shuso_set_error(S, "can't initialize event from outside a shuttlesock module");
  }
  Lua_push_module_function(L, "initialize_event");
  lua_pushlightuserdata(L, mod);
  lua_pushstring(L, name);
  lua_pushlightuserdata(L, mev);
  return lua_function_call_result_ok(S, L, 3, false);
}

bool shuso_module_event_listen(shuso_t *S, const char *name, shuso_module_event_fn *callback, void *pd) {
  lua_State       *L = S->lua.state;
  shuso_module_t  *module;
  
  Lua_push_module_function(L, "currently_initializing_module");
  if(!lua_function_call_result_ok(S, L, 1, true)) {
    return false;
  }
  lua_getfield(L, -1, "ptr");
  
  module = (void *)lua_topointer(L, -1);
  lua_pop(L, 2);
  
  Lua_push_module_function(L, "find_event");
  lua_pushstring(L, name);
  if(!lua_function_call_result_ok(S, L, 1, true)) {
    return false;
  }
  
  lua_getfield(L, -1, "add_listener");
  lua_pushvalue(L, -2);
  lua_pushlightuserdata(L, module);
  lua_pushlightuserdata(L, *((void **)&callback));
  lua_pushlightuserdata(L, pd);
  if(!lua_function_call_result_ok(S, L, 4, false)) {
    lua_pop(L, 1);
    return false;
  }
  lua_pop(L, 1);
  return true;
  
}

bool shuso_module_finalize(shuso_t *S, shuso_module_t *mod) {
  lua_State      *L = S->lua.state;
  int             n;
  if(mod == NULL) {
    return shuso_set_error(S, "can't finalize events from outside a shuttlesock module");
  }
  
  Lua_push_module_function(L, "find");
  lua_pushlightuserdata(L, mod);
  if(!lua_function_call_result_ok(S, L, 1, true)) {
    return false;
  }
  
  
  lua_getfield(L, -1, "all_events_initialized");
  lua_pushvalue(L, -2);
  if(!lua_function_call_result_ok(S, L, 1, false)) {
    return false;
  }
  
  lua_getfield(L, -1, "finalize");
  lua_pushvalue(L, -2);
  if(!lua_function_call_result_ok(S, L, 1, false)) {
    return false;
  }
  
  lua_getfield(L, -1, "parent_modules");
  n = luaL_len(L, -1);
  mod->submodules.count = n;
  if((mod->submodules.array = shuso_stalloc(&S->stalloc, sizeof(*mod->submodules.array) * n)) == NULL) {
    return shuso_set_error(S, "failed to allocate memory for submodules array");
  }
  if((mod->submodules.context = shuso_stalloc(&S->stalloc, sizeof(*mod->submodules.context) * n)) == NULL) {
    return shuso_set_error(S, "failed to allocate memory for submodules context");
  }
  
  for(int i=1; i<=n; i++) {
    lua_rawgeti(L, -1, i);
    
    lua_getfield(L, -1, "ptr");
    mod->submodules.array[i]=(void *)lua_topointer(L, -1);
    mod->submodules.context[i] = NULL;
    
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  
  lua_getfield(L, -1, "events");
  lua_getfield(L, -1, "publish");
  n = luaL_len(L, -1);
  for(int i=1; i<=n; i++) {
    lua_rawgeti(L, -1, i);
    
    shuso_module_event_t *event;
    
    lua_getfield(L, -1, "ptr");
    event = (void *)lua_topointer(L, -1);
    assert(event != NULL);
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "name");
    event->name = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    lua_getfield(L, -1, "listeners");
    n = luaL_len(L, -1);
    
    shuso_module_event_listener_t *cur;
    shuso_module_event_listener_t *listeners = shuso_stalloc(&S->stalloc, sizeof(*listeners) * (n+1));
    if(listeners == NULL) {
      return shuso_set_error(S, "failed to allocate memory for event listeners");
    }
    for(int j=0; j<n; i++) {
      cur = &listeners[j];
      lua_rawgeti(L, -1, j+1);
      
      lua_getfield(L, -1, "module");
      lua_getfield(L, -1, "ptr");
      cur->module = (void *)lua_topointer(L, -1);
      lua_pop(L, 1);
      
      lua_getfield(L, -1, "listener");
      cur->fn = (shuso_module_event_fn *)lua_topointer(L, -1);
      lua_pop(L, 1);
      
      lua_getfield(L, -1, "privdata");
      cur->pd = (void *)lua_topointer(L, -1);
      lua_pop(L, 1);
      
      lua_pop(L, 1);
      
      Lua_push_module_function(L, "dependency_index");
      lua_pushstring(L, mod->name);
      lua_pushstring(L, cur->module->name);
      if(!lua_function_call_result_ok(S, L, 2, true)) {
        return false;
      }
      lua_pop(L, 1);
    }
    listeners[n] = (shuso_module_event_listener_t ) {
      .module = NULL,
      .fn = NULL
    };
    event->listeners = listeners;
  }
  lua_pop(L, 1);
  
  lua_getfield(L, -1, "dependent_modules_count");
  lua_pushvalue(L, -2);
  if(!lua_function_call_result_ok(S, L, 1, true)) {
    return false;
  }
  mod->context_count = lua_tonumber(L, -1);
  lua_pop(L, 1);
  
  lua_pop(L, 1); //pop module
  return true;
}

bool shuso_module_event_publish(shuso_t *S, shuso_module_event_t *event, intptr_t code, void *data) {
  return false;
}

shuso_module_t *shuso_start_event_system(shuso_t *S, shuso_module_t *core_module) {
  
  return NULL;
}
