#ifndef LUA_IPC_H
#define LUA_IPC_H
#include <shuttlesock/common.h>
bool shuso_register_lua_ipc_handler(shuso_t *S);

int luaS_ipc_send_message_yield(lua_State *L);

#endif //LUA_IPC_H
