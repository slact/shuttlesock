#include <shuttlesock.h>
#include <lauxlib.h>
#include "lua_ipc.h"



static bool pack_field(lua_State *L, shuso_ipc_lua_field_t *field, int n, int reftable_n, int tables_n) {
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
      lua_pushvalue(L, tables_n);
      lua_pushvalue(L, n);
      lua_gettable(L, -2);
      lua_remove(L, -2);
      if(lua_isuserdata(L, -1)) {
        field->type = LUA_TTABLE;
        field->table = (void *)lua_topointer(L, -1);
        lua_settop(L, top);
        return true;
      }
      lua_pop(L, 1);
      
      field->type = LUA_TTABLE;
      int count = 0;
      lua_checkstack(L, 3);
      lua_pushnil(L);
      while(lua_next(L, n)) {
        lua_pop(L, 1);
        count++;
      }
      field->table = lua_newuserdata(L, sizeof(*field->table) + sizeof(shuso_ipc_lua_field_t)*count*2);
      field->table->keys = (void *)&field->table[1];
      field->table->values = (void *)&field->table->keys[count];
      
      int narr = 0, nrec = 0, i = 0;
      
      shuso_ipc_lua_field_t *keys = field->table->keys;
      shuso_ipc_lua_field_t *values = field->table->values;
      lua_pushvalue(L, -1);
      luaL_ref(L, reftable_n);
      
      lua_pushvalue(L, n);
      lua_pushvalue(L, -2);
      lua_settable(L, tables_n);
      lua_pop(L, 1);
      //raise(SIGABRT);
      
      lua_pushnil(L);
      while(lua_next(L, n)) {
        lua_isinteger(L, -2) ? narr++ : nrec++;
        if(!pack_field(L, &values[i], lua_gettop(L), reftable_n, tables_n)) {
          lua_settop(L, top);
          return false;
        }
        lua_pop(L, 1);
        if(!pack_field(L, &keys[i], lua_gettop(L), reftable_n, tables_n)) {
          lua_settop(L, top);
          return false;
        }
        i++;
      }
      
      field->table->narr = narr;
      field->table->nrec = nrec;
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

static bool unpack_field(lua_State *L, shuso_ipc_lua_field_t *field, int table_n) {
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
      
      lua_pushlightuserdata(L, table);
      lua_gettable(L, table_n);
      if(lua_istable(L, -1)) {
        return true;
      }
      lua_pop(L, 1);
      
      lua_createtable(L, table->narr, table->nrec);
      
      lua_pushlightuserdata(L, table);
      lua_pushvalue(L, -2);
      lua_settable(L, table_n);
      
      int sz = table->narr + table->nrec;
      for(int i=0; i<sz; i++) {
        if(!unpack_field(L, &table->keys[i], table_n)) {
          lua_settop(L, top);
          return false;
        }
        if(!unpack_field(L, &table->values[i], table_n)) {
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

shuso_ipc_lua_data_t *luaS_lua_ipc_pack_data(lua_State *L, int index) {
  int top = lua_gettop(L);
  index = lua_absindex(L, index);
  lua_checkstack(L, 3);
  
  lua_newtable(L);
  int table_n = lua_gettop(L);
  
  lua_createtable(L, 2, 0);
  int reftable_n = lua_gettop(L);
  
  shuso_ipc_lua_data_t *data = lua_newuserdata(L, sizeof(*data));
  luaL_ref(L, reftable_n);
  
  if(!pack_field(L, &data->field, index, reftable_n, table_n)) {
    lua_settop(L, top);
    return NULL;
  }
  assert(lua_compare(L, -1, reftable_n, LUA_OPEQ));
  data->reftable = luaL_ref(L, LUA_REGISTRYINDEX); //reference the reftable
  lua_pop(L, 1); //pop table-of-tables
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

bool luaS_lua_ipc_unpack_data(lua_State *L, shuso_ipc_lua_data_t *d) {
  lua_newtable(L);
  int table_n = lua_gettop(L);
  bool ok = unpack_field(L, &d->field, table_n);
  lua_remove(L, table_n);
  return ok;
}

static void lua_ipc_handler(shuso_t *S, const uint8_t code, void *ptr) {
  shuso_ipc_lua_data_t  *d = ptr;
  lua_State       *L = S->lua.state;
  
  lua_getfield(L, LUA_REGISTRYINDEX, "shuttlesock.lua_ipc.response_code");
  int response_code = lua_tonumber(L, -1);
  lua_pop(L, 1);
  
  luaS_push_lua_module_field(L, "shuttlesock.ipc", "receive_from_shuttlesock_core");
  lua_pushstring(L, d->name);
  lua_pushnumber(L, d->sender);
  luaS_lua_ipc_unpack_data(L, d);
  luaS_pcall(L, 3, 1);
  
  d->success = lua_toboolean(L, -1);
  lua_pop(L, 1);
  shuso_ipc_send(S, &S->common->process.worker[d->sender], response_code, d);
}

bool luaS_lua_ipc_gc_data(lua_State *L, shuso_ipc_lua_data_t *d) {
  luaL_unref(L, LUA_REGISTRYINDEX, d->reftable);
  return true;
}

static void lua_ipc_cancel_handler(shuso_t *S, const uint8_t code, void *ptr) {
  shuso_ipc_lua_data_t  *d = ptr;
  lua_State       *L = S->lua.state;
  lua_rawgeti(L, LUA_REGISTRYINDEX, d->reftable);
  lua_getfield(L, -1, "caller");
  lua_remove(L, -2);  //clean up the reftable
  luaS_lua_ipc_gc_data(L, d);
  shuso_log_warning(S, "cancel!");
  lua_ipc_return_to_caller(S, L, false);
}

static void lua_ipc_response_handler(shuso_t *S, const uint8_t code, void *ptr) {
  shuso_ipc_lua_data_t  *d = ptr;
  lua_State       *L = S->lua.state;
  bool             success = d->success;
  lua_rawgeti(L, LUA_REGISTRYINDEX, d->reftable);
  lua_getfield(L, -1, "caller");
  
  //clean up the reftable
  lua_remove(L, -2);
  luaS_lua_ipc_gc_data(L, d);
  d = NULL; // don't use it anymore, it may be GCed anytime
  
  lua_ipc_return_to_caller(S, L, success);
}

static void lua_ipc_response_cancel_handler(shuso_t *S, const uint8_t code, void *ptr) {
  //TODO
}


int lua_ipc_send_message(lua_State *L, bool yield) {
  luaL_checknumber(L, 1);
  luaL_checkstring(L, 2);
  shuso_t         *S = shuso_state(L);
  int              dst = lua_tointeger(L, 1);
  shuso_ipc_lua_data_t  *data = luaS_lua_ipc_pack_data(L, 3);
  int              ipc_code;
  data->name = lua_tostring(L, 2);
  
  if(!shuso_procnum_valid(S, dst)) {
    return luaL_error(L, "invalid destination");
  }
  
  lua_getfield(L, LUA_REGISTRYINDEX, "shuttlesock.lua_ipc.code");
  
  ipc_code = lua_tointeger(L, -1);
  assert(ipc_code > 0);
  lua_pop(L, 1);
  
  data->sender = S->procnum;
  
  if(yield) {
    assert(lua_isyieldable(L));
    lua_rawgeti(L, LUA_REGISTRYINDEX, data->reftable);
    lua_pushthread(L);
    lua_setfield(L, -2, "caller");
  }
  
  //make sure the ipc message name stays allocd
  lua_pushvalue(L, 2);
  luaL_ref(L, -2);
  
  shuso_ipc_send(S, &S->common->process.worker[dst], ipc_code, data);
  
  lua_pushboolean(L, 1);
  if(yield) {
    return lua_yield(L, 1);
  }
  else {
    return 1;
  }
}

int luaS_ipc_send_message_yield(lua_State *L) {
  return lua_ipc_send_message(L, true);
}
int luaS_ipc_send_message_noyield(lua_State *L) {
  return lua_ipc_send_message(L, false);
}

bool shuso_register_lua_ipc_handler(shuso_t *S) {
  lua_State                 *L= S->lua.state;
  const shuso_ipc_handler_t *handler;
  
  if((handler = shuso_ipc_add_handler(S, "lua_ipc", SHUTTLESOCK_IPC_CODE_AUTOMATIC, lua_ipc_handler, lua_ipc_cancel_handler)) == NULL) {
    return false;
  }
  int send_code = handler->code;
  
  if((handler = shuso_ipc_add_handler(S, "lua_ipc_response", SHUTTLESOCK_IPC_CODE_AUTOMATIC, lua_ipc_response_handler, lua_ipc_response_cancel_handler)) == NULL) {
    return false;
  }  
  int send_resposne_code = handler->code;
  
  luaS_push_lua_module_field(L, "shuttlesock.ipc", "set_ipc_codes");
  lua_pushinteger(L, send_code);
  lua_pushinteger(L, send_resposne_code);
  luaS_pcall(L, 2, 0);
  
  return true;
}
