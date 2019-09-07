#include <shuttlesock.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

//create a shuttlesock instance from inside Lua
int Lua_shuso_create(lua_State *L) {
  return 0;
}

int Lua_shuso_get_current_shuttlesock_instance(lua_State *L) {
  return 0;
}

luaL_Reg shuttlesock_module_methods[] = {
  {"create", Lua_shuso_create},
  {"get", Lua_shuso_get_current_shuttlesock_instance},
  {NULL, NULL}
};

int Lua_shuso_configure_file(lua_State *L) {
  return 0;
}
int Lua_shuso_configure_string(lua_State *L) {
  return 0;
}
int Lua_shuso_configure_handlers(lua_State *L) {
  return 0;
}
int Lua_shuso_configure_finish(lua_State *L) {
  return 0;
}

int Lua_shuso_destroy(lua_State *L) {
  return 0;
}

int Lua_shuso_run(lua_State *L) {
  return 0;
}
int Lua_shuso_stop(lua_State *L) {
  return 0;
}

int Lua_shuso_spawn_manager(lua_State *L) {
  return 0;
}
int Lua_shuso_stop_manager(lua_State *L) {
  return 0;
}
int Lua_shuso_spawn_worker(lua_State *L) {
  return 0;
}
int Lua_shuso_stop_worker(lua_State *L) {
  return 0;
}

int Lua_shuso_set_log_fd(lua_State *L) {
  return 0;
}

int Lua_shuso_set_error(lua_State *L) {
  return 0;
}

//watchers
int Lua_shuso_add_timer_watcher(lua_State *L) {
  return 0;
}
int Lua_shuso_remove_timer_watcher(lua_State *L) {
  return 0;
}

//shared memory slab
int Lua_shuso_shared_slab_alloc_string(lua_State *L) {
  return 0;
}
int Lua_shuso_shared_slab_free_string(lua_State *L) {
  return 0;
}

//resolver
int Lua_shuso_resolve_hostname(lua_State *L) {
  return 0;
}

//logger
int Lua_shuso_log(lua_State *L) {
  return 0;
}

//ipc
int Lua_shuso_ipc_send_fd(lua_State *L) {
  return 0;
}
int Lua_shuso_ipc_receive_fd(lua_State *L) {
  return 0;
}

int Lua_shuso_ipc_open_listener_sockets(lua_State *L) {
  return 0;
}

int Lua_shuso_ipc_add_handler(lua_State *L) {
  return 0;
}
int Lua_shuso_ipc_send(lua_State *L) {
  return 0;
}
int Lua_shuso_ipc_send_workers(lua_State *L) {
  return 0;
}



luaL_Reg shuttlesock_object_methods[] = {
  {"configureFile", Lua_shuso_configure_file},
  {"configureString", Lua_shuso_configure_string},
  {"configureHandlers", Lua_shuso_configure_handlers},
  {"configureFinish", Lua_shuso_configure_finish},
  
  {"destroy", Lua_shuso_destroy},
  
  {"run", Lua_shuso_run},
  {"stop", Lua_shuso_stop},
  
  {"spawnManager", Lua_shuso_spawn_manager},
  {"stopManager", Lua_shuso_stop_manager},
  
  {"spawnWorker", Lua_shuso_spawn_worker},
  {"stopWorker", Lua_shuso_stop_worker},
  
  {"setLogFile", Lua_shuso_set_log_fd},
  
  {"setError", Lua_shuso_set_error},
    
//watchers
  {"addTimer", Lua_shuso_add_timer_watcher},
  {"removeTimer", Lua_shuso_remove_timer_watcher},
  
//shared slab
  {"sharedSlabAllocString", Lua_shuso_shared_slab_alloc_string},
  {"sharedSlabFreeString", Lua_shuso_shared_slab_free_string},

//resolver
  {"resolve", Lua_shuso_resolve_hostname},
  
//logger
  {"log", Lua_shuso_log},

//ipc
  {"sendFile", Lua_shuso_ipc_send_fd},
  {"receiveFile", Lua_shuso_ipc_receive_fd},
  {"openListenerSockets", Lua_shuso_ipc_open_listener_sockets},
  {"addMessageHandler", Lua_shuso_ipc_add_handler},
  {"sendMessage", Lua_shuso_ipc_send},
  {"sendMessageToAllWorkers", Lua_shuso_ipc_send_workers},
  
  {NULL, NULL}
};

int shuso_Lua_binding_module(lua_State *L) {
  if(luaL_newmetatable(L, "shuttlesock_userdata") != 0) {
    //metatable for shuttlesock userdata object
    luaL_newlib(L, shuttlesock_object_methods);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
  }
  
  //now the module
  luaL_newlib(L, shuttlesock_module_methods);
  return 1;
}
