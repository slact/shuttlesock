#ifndef SHUTTLESOCK_MODULE_LUA_BRIDGE_H
#define SHUTTLESOCK_MODULE_LUA_BRIDGE_H

extern shuso_module_t shuso_lua_bridge_module;

typedef struct {
  int           ipc_messages_active;
} shuso_lua_bridge_module_ctx_t;

#endif //SHUTTLESOCK_MODULE_LUA_BRIDGE_H
