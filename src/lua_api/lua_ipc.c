#include <shuttlesock.h>
#include <lauxlib.h>
#include "lua_ipc.h"

typedef struct {
  const char *data;
  size_t      sz;
} ipc_lua_data_string_t;

typedef struct ipc_lua_field_s ipc_lua_field_t;

typedef struct {
  ipc_lua_field_t *keys;
  ipc_lua_field_t *values;
  int narr;
  int nrec;
} ipc_lua_table_t;

struct ipc_lua_field_s {
  uint8_t                type;
  uint8_t                is_integer;
  union {
    int                    integer;
    double                 number;
    ipc_lua_data_string_t  string;
    bool                   boolean;
    void                  *pointer;
    ipc_lua_table_t       *table;
  };
}; // ipc_lua_field_t

typedef struct {
  ipc_lua_field_t    field;
  lua_reference_t    reftable;
  const char        *name;
  int                sender;
  bool               success;
} ipc_lua_data_t;

static bool pack_field(lua_State *L, ipc_lua_field_t *field, int n, int reftable_n) {
  int type = lua_type(L, n);
  int top = lua_gettop(L);
  
  switch(type) {
    case LUA_TNIL:
      field->type = LUA_TNIL;
      return true;
      
    case LUA_TBOOLEAN:
      field->type = LUA_TBOOLEAN;
      field->boolean = lua_toboolean(L, n);
      return true;
      
    case LUA_TLIGHTUSERDATA:
      field->type = LUA_TLIGHTUSERDATA;
      field->pointer = (void *)lua_topointer(L, n);
      return true;
      
    case LUA_TNUMBER:
      field->type = LUA_TNUMBER;
      if(lua_isinteger(L, n)) {
        field->is_integer = 1;
        field->integer = lua_tointeger(L, n);
      }
      else {
        field->is_integer = 0;
        field->number = lua_tonumber(L, n);
      }
      return true;
      
    case LUA_TSTRING:
      field->type = LUA_TSTRING;
      lua_pushvalue(L, n);
      luaL_ref(L, reftable_n);
      field->string.data = lua_tolstring(L, n, &field->string.sz);
      return true;
      
    case LUA_TTABLE: {
      field->type = LUA_TTABLE;
      int sz = 0;
      lua_checkstack(L, 3);
      lua_pushnil(L);
      while(lua_next(L, n)) {
        lua_pop(L, 1);
        sz++;
      }
      
      field->table = lua_newuserdata(L, sizeof(*field->table) + sizeof(ipc_lua_field_t)*sz*2);
      field->table->keys = (void *)&field->table[1];
      field->table->values = (void *)&((char *)field->table->keys)[sz];
      
      int narr = 0, nrec = 0, i = 0;
      
      ipc_lua_field_t *keys = field->table->keys;
      ipc_lua_field_t *values = field->table->values;
      
      lua_pushnil(L);
      while(lua_next(L, n)) {
        lua_isinteger(L, -2) ? narr++ : nrec++;
        if(!pack_field(L, &values[i], -1, reftable_n)) {
          lua_settop(L, top);
          return false;
        }
        lua_pop(L, 1);
        if(!pack_field(L, &keys[i], -1, reftable_n)) {
          lua_settop(L, top);
          return false;
        }
        i++;
      }
      assert(lua_topointer(L, -1) == field->table);
      luaL_ref(L, reftable_n);
      return true;
    }
    
    case LUA_TFUNCTION:
      return luaL_error(L, "cannot send function in Lua IPC message");
      
    case LUA_TUSERDATA:
      return luaL_error(L, "cannot send full userdata in Lua IPC message");
      
    case LUA_TTHREAD:
      return luaL_error(L, "cannot send coroutine in Lua IPC message");
      
    default:
      return luaL_error(L, "invalid type while trying to send a Lua IPC message");
  }
}

static bool unpack_field(lua_State *L, ipc_lua_field_t *field) {
  int top = lua_gettop(L);
  switch(field->type) {
    case LUA_TNIL:
      lua_pushnil(L);
      return true;
      
    case LUA_TBOOLEAN:
      lua_pushboolean(L, field->boolean);
      return true;
      
    case LUA_TLIGHTUSERDATA:
      lua_pushlightuserdata(L, field->pointer);
      return true;
      
    case LUA_TNUMBER:
      if(field->is_integer) {
        lua_pushinteger(L, field->integer);
      }
      else {
        lua_pushnumber(L, field->number);
      }
      return true;
      
    case LUA_TSTRING:
      lua_pushlstring(L, field->string.data, field->string.sz);
      return true;
      
    case LUA_TTABLE: {
      ipc_lua_table_t *table = field->table;
      lua_createtable(L, table->narr, table->nrec);
      int sz = table->narr + table->nrec;
      for(int i=0; i<sz; i++) {
        if(!unpack_field(L, &table->keys[i])) {
          lua_settop(L, top);
          return false;
        }
        if(!unpack_field(L, &table->values[i])) {
          lua_settop(L, top);
          return false;
        }
        lua_settable(L, -3);
      }
      return true;
    }
    
    default:
      return luaL_error(L, "bad type in Lua IPC message");
  }
}

static ipc_lua_data_t *lua_ipc_pack_data(lua_State *L, int index) {
  int top = lua_gettop(L);
  index = lua_absindex(L, index);
  lua_checkstack(L, 2);
  lua_createtable(L, 2, 0);
  int reftable_n = lua_gettop(L);
  ipc_lua_data_t *data = lua_newuserdata(L, sizeof(*data));
  
  lua_pushvalue(L, -1);
  luaL_ref(L, reftable_n);
  
  if(!pack_field(L, &data->field, index, reftable_n)) {
    lua_settop(L, top);
    return NULL;
  }
  
  assert(lua_compare(L, -1, reftable_n, LUA_OPEQ));
  data->reftable = luaL_ref(L, LUA_REGISTRYINDEX); //reference the reftable
  assert(lua_gettop(L) == top);
  return data;
}

static void lua_ipc_return_to_caller(shuso_t *S, lua_State *L, bool success) {
  if(lua_isfunction(L, -1)) {
    lua_pushboolean(L, success);
    if(!success) {
      lua_pushstring(L, "something went wrong");
    }
    luaS_pcall(L, success ? 1 : 2, 0);
  }
  else if(lua_isthread(L, -1)) {
    lua_State *thread = lua_tothread(L, -1);
    int threadtop = lua_gettop(thread);
    lua_pushboolean(thread, success);
    if(!success) {
      lua_pushstring(thread, "something went wrong");
    }
    if(luaS_resume(thread, L, success ? 1 : 2) == LUA_YIELD) {
      //discard yielded values
      lua_settop(thread, threadtop);
    }
  }
  else {
    assert(lua_isnil(L, -1));
    lua_pop(L, 1);
  }
}

static void lua_ipc_handler(shuso_t *S, const uint8_t code, void *ptr) {
  ipc_lua_data_t  *d = ptr;
  lua_State       *L = S->lua.state;
  
  lua_getfield(L, LUA_REGISTRYINDEX, "shuttlesock.lua_ipc.response_code");
  int response_code = lua_tonumber(L, -1);
  lua_pop(L, 1);
  
  luaS_push_lua_module_field(L, "shuttlesock.ipc", "receive_from_shuttlesock_core");
  lua_pushstring(L, d->name);
  lua_pushnumber(L, d->sender);
  unpack_field(L, &d->field);
  luaS_pcall(L, 3, 1);
  
  d->success = lua_toboolean(L, -1);
  lua_pop(L, 1);
  shuso_ipc_send(S, &S->common->process.worker[d->sender], response_code, d);
}

static void lua_ipc_cancel_handler(shuso_t *S, const uint8_t code, void *ptr) {
  ipc_lua_data_t  *d = ptr;
  lua_State       *L = S->lua.state;;
  lua_rawgeti(L, LUA_REGISTRYINDEX, d->reftable);
  lua_getfield(L, -1, "caller");
  
  //clean up the reftable
  lua_remove(L, -2);
  luaL_unref(L, LUA_REGISTRYINDEX, d->reftable);
  
  lua_ipc_return_to_caller(S, L, false);
}

static void lua_ipc_response_handler(shuso_t *S, const uint8_t code, void *ptr) {
  ipc_lua_data_t  *d = ptr;
  lua_State       *L = S->lua.state;
  bool             success = d->success;
  lua_rawgeti(L, LUA_REGISTRYINDEX, d->reftable);
  lua_getfield(L, -1, "caller");
  
  //clean up the reftable
  lua_remove(L, -2);
  luaL_unref(L, LUA_REGISTRYINDEX, d->reftable);
  d = NULL; // don't use it anymore, it may be GCed anytime
  
  lua_ipc_return_to_caller(S, L, success);
}

static void lua_ipc_response_cancel_handler(shuso_t *S, const uint8_t code, void *ptr) {
  //TODO
}


int luaS_ipc_send_message_yield(lua_State *L) {
  luaL_checknumber(L, 1);
  luaL_checkstring(L, 2);
  shuso_t         *S = shuso_state(L);
  int              dst = lua_tointeger(L, 1);
  ipc_lua_data_t  *data = lua_ipc_pack_data(L, 3);
  int              ipc_code;
  data->name = lua_tostring(L, 2);
  
  if(!shuso_procnum_valid(S, dst)) {
    return luaL_error(L, "invalid destination");
  }
  
  lua_getfield(L, LUA_REGISTRYINDEX, "shuttlesock.lua_ipc.code");
  
  ipc_code = lua_tointeger(L, -1);
  lua_pop(L, 1);
  
  data->sender = S->procnum;
  assert(lua_isyieldable(L));
  lua_rawgeti(L, LUA_REGISTRYINDEX, data->reftable);
  lua_pushthread(L);
  lua_setfield(L, -2, "caller");
  
  //make sure the ipc message name stays allocd
  lua_pushvalue(L, 2);
  luaL_ref(L, -2);
  
  shuso_ipc_send(S, &S->common->process.worker[dst], ipc_code, data);
  
  lua_pushboolean(L, 1);
  return lua_yield(L, 1);
}

bool shuso_register_lua_ipc_handler(shuso_t *S) {
  lua_State                 *L= S->lua.state;
  const shuso_ipc_handler_t *handler;
  
  if((handler = shuso_ipc_add_handler(S, "lua_ipc", SHUTTLESOCK_IPC_CODE_AUTOMATIC, lua_ipc_handler, lua_ipc_cancel_handler)) == NULL) {
    return false;
  }
  lua_pushinteger(L, handler->code);
  lua_setfield(L, LUA_REGISTRYINDEX, "shuttlesock.lua_ipc.code");
  
  if((handler = shuso_ipc_add_handler(S, "lua_ipc_response", SHUTTLESOCK_IPC_CODE_AUTOMATIC, lua_ipc_response_handler, lua_ipc_response_cancel_handler)) == NULL) {
    return false;
  }
  lua_pushinteger(L, handler->code);
  lua_setfield(L, LUA_REGISTRYINDEX, "shuttlesock.lua_ipc.response_code");
  return true;
}
