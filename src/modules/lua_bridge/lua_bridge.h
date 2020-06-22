#ifndef SHUTTLESOCK_MODULE_LUA_BRIDGE_H
#define SHUTTLESOCK_MODULE_LUA_BRIDGE_H

extern shuso_module_t shuso_lua_bridge_module;

typedef struct {
  _Atomic(int)        ipc_messages_active;
} shuso_lua_bridge_module_common_ctx_t;

typedef struct {
  bool                (*wrap)(lua_State *L, const char *type, void *data);
  bool                (*wrap_cleanup)(lua_State *L, const char *type, void *data);
  
  lua_reference_t     (*unwrap)(lua_State *L, const char *type, int narg, void **ret);
  bool                (*unwrap_cleanup)(lua_State *L, const char *datatype, lua_reference_t ref, void *data);
} shuso_lua_event_data_wrapper_t;


bool shuso_lua_event_register_data_wrapper(shuso_t *S, const char *name, shuso_lua_event_data_wrapper_t *wrapper);

#endif //SHUTTLESOCK_MODULE_LUA_BRIDGE_H
