#ifndef LUA_IPC_H
#define LUA_IPC_H
#include <shuttlesock/common.h>
bool shuso_register_lua_ipc_handler(shuso_t *S);
int luaS_ipc_send_message(lua_State *L);

typedef struct {
  const char *data;
  size_t      sz;
} shuso_ipc_lua_data_string_t;

typedef struct shuso_ipc_lua_field_s shuso_ipc_lua_field_t;

typedef struct {
  shuso_ipc_lua_field_t *keys;
  shuso_ipc_lua_field_t *values;
  int narr;
  int nrec;
} ipc_lua_table_t;

typedef struct shuso_ipc_lua_field_s {
  uint8_t                type;
  uint8_t                is_integer;
  union {
    int                         integer;
    double                      number;
    shuso_ipc_lua_data_string_t string;
    bool                        boolean;
    void                       *pointer;
    ipc_lua_table_t            *table;
  };
} shuso_ipc_lua_field_t;

typedef struct {
  shuso_ipc_lua_field_t field;
  lua_reference_t       reftable;
  const char           *name;
  int                   origin_procnum;
  bool                  success;
  bool                  in_shared_memory;
  bool                  automatic_gc;
} shuso_ipc_lua_data_t;

shuso_ipc_lua_data_t *luaS_lua_ipc_pack_data(lua_State *L, int index, const char *name, bool use_shared_memory);
bool luaS_lua_ipc_unpack_data(lua_State *L, shuso_ipc_lua_data_t *d);
bool luaS_lua_ipc_gc_data(lua_State *L, shuso_ipc_lua_data_t *d);

#endif //LUA_IPC_H
