#include <shuttlesock.h>
#include <lauxlib.h>
#include "lua_ipc.h"


static void *get_already_packed_field(lua_State *L, int data_index, int packed_data_table_index) {
  //do we have this data already?
  int ttop = lua_gettop(L);
  luaL_checkstack(L, 2, NULL);
  lua_pushvalue(L, packed_data_table_index);
  lua_pushvalue(L, data_index);
  lua_gettable(L, -2);
  lua_remove(L, -2);
  
  void *ptr = lua_isnil(L, -1) ? NULL : (void *)lua_topointer(L, -1);
  lua_pop(L, 1);
  assert(lua_gettop(L)==ttop);
  return ptr;
}


static bool pack_field(lua_State *L, shuso_ipc_lua_data_t *data, shuso_ipc_lua_field_t *field, int n, int packed_data_table_index) {
  //luaS_printstack(L, "pack_field start");
  assert(lua_type(L, packed_data_table_index) == LUA_TTABLE);
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
      
    case LUA_TSTRING: {
      field->type = LUA_TSTRING;
      if(data->in_shared_memory) {
        int ttop = lua_gettop(L);
        const char *str = lua_tolstring(L, n, &field->string.sz);
        shuso_t    *S = shuso_state(L);
        field->string.data = shuso_shared_slab_alloc(&S->common->shm, field->string.sz);
        memcpy((char *)field->string.data, str, field->string.sz);

        //save pointer for later cleanup
        lua_rawgeti(L, LUA_REGISTRYINDEX, data->reftable);
        lua_pushlightuserdata(L, (void *)field->string.data);
        lua_pushboolean(L, 1);
        lua_rawset(L, -3);
        lua_pop(L, 1);
        assert(ttop == lua_gettop(L));
      }
      else {
        lua_rawgeti(L, LUA_REGISTRYINDEX, data->reftable);
        lua_pushvalue(L, n);
        luaL_ref(L, -2);
        lua_pop(L, 1);
        field->string.data = lua_tolstring(L, n, &field->string.sz);
      }
      return true;
    }
      
    case LUA_TTABLE: {
      field->type = LUA_TTABLE;
      
      ipc_lua_table_t *table = get_already_packed_field(L, n, packed_data_table_index);
      if(table) {
        field->table = table;
        lua_settop(L, top);
        return true;
      }
      
      int count = 0;
      lua_checkstack(L, 3);
      lua_pushnil(L);
      while(lua_next(L, n)) {
        lua_pop(L, 1);
        count++;
      }
      
      size_t sz = sizeof(*field->table) + sizeof(shuso_ipc_lua_field_t)*count*2;
      if(data->in_shared_memory) {
        shuso_t *S = shuso_state(L);
        field->table = shuso_shared_slab_alloc(&S->common->shm, sz);
        
        //save pointer for later cleanup
        lua_rawgeti(L, LUA_REGISTRYINDEX, data->reftable);
        lua_pushlightuserdata(L, field->table);
        lua_pushboolean(L, 1);
        lua_rawset(L, -3);
        lua_pop(L, 1);
        
        //push pointer to store in the packed_data_table
        lua_pushlightuserdata(L, field->table);
      }
      else {
        field->table = lua_newuserdata(L, sz);
        
        //save reference so that it doesn't get GC'd
        lua_rawgeti(L, LUA_REGISTRYINDEX, data->reftable);
        lua_pushvalue(L, -2);
        luaL_ref(L, -2);
        lua_pop(L, 1);
      }
      
      //save in table list to handle recursive tables while packing
      lua_pushvalue(L, n);
      lua_pushvalue(L, -2);
      lua_settable(L, packed_data_table_index);
      lua_pop(L, 1);
      
      field->table->keys = (void *)&field->table[1];
      field->table->values = (void *)&field->table->keys[count];
      
      int narr = 0, nrec = 0, i = 0;
      
      shuso_ipc_lua_field_t *keys = field->table->keys;
      shuso_ipc_lua_field_t *values = field->table->values;
      
      //raise(SIGABRT);
      
      lua_pushnil(L);
      while(lua_next(L, n)) {
        lua_isinteger(L, -2) ? narr++ : nrec++;
        if(!pack_field(L, data, &values[i], lua_gettop(L), packed_data_table_index)) {
          lua_settop(L, top);
          return false;
        }
        lua_pop(L, 1);
        if(!pack_field(L, data, &keys[i], lua_gettop(L), packed_data_table_index)) {
          lua_settop(L, top);
          return false;
        }
        i++;
      }
      
      field->table->narr = narr;
      field->table->nrec = nrec;
      assert(top == lua_gettop(L));
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

shuso_ipc_lua_data_t *luaS_lua_ipc_pack_data(lua_State *L, int index, const char *name, bool use_shared_memory) {
  shuso_t    *S = shuso_state(L);
  int         top = lua_gettop(L);
  index = lua_absindex(L, index);
  lua_checkstack(L, 3);
  
  lua_newtable(L);
  int         tables_n = lua_gettop(L);
  shuso_ipc_lua_data_t *data;
  
  if(use_shared_memory) {
    data = shuso_shared_slab_alloc(&S->common->shm, sizeof(*data) + strlen(name)+1);
    
    lua_newtable(L); //reftable
    data->reftable = luaL_ref(L, LUA_REGISTRYINDEX);
    
    data->name = (const char *)&data[1];
    data->in_shared_memory = true;
    strcpy((char *)data->name, name);
  }
  else {
    lua_createtable(L, 2, 0); //reftable
    data = lua_newuserdata(L, sizeof(*data));
    luaL_ref(L, -2);
    
    lua_pushstring(L, name);
    lua_pushvalue(L, -1);
    luaL_ref(L, -3);
    data->name = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    data->reftable = luaL_ref(L, LUA_REGISTRYINDEX);
    
    data->in_shared_memory = false;
  }
  if(!pack_field(L, data, &data->field, index, tables_n)) {
    lua_settop(L, top);
    return NULL;
  }
  lua_settop(L, top);
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
  shuso_ipc_send(S, shuso_procnum_to_process(S, d->sender), response_code, d);
}

bool luaS_lua_ipc_gc_data(lua_State *L, shuso_ipc_lua_data_t *d) {
  if(d->in_shared_memory) {
    shuso_t *S = shuso_state(L);
    luaL_checkstack(L, 4, NULL);
    lua_rawgeti(L, LUA_REGISTRYINDEX, d->reftable);
    lua_pushnil(L);
    while(lua_next(L, -2)) {
      lua_pop(L, 1);
      if(lua_type(L, -1) == LUA_TLIGHTUSERDATA) {
        shuso_shared_slab_free(&S->common->shm, (void *)lua_topointer(L, -1));
      }
    }
    lua_pop(L, 1);
  }
  
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
  //nothing needs to be done
}


static int lua_ipc_send_message(lua_State *L, bool yield) {
  int                    dst = luaL_checkinteger(L, 1);
  int                    top = lua_gettop(L);
  shuso_t               *S = shuso_state(L);
  shuso_process_t       *dst_proc = shuso_procnum_to_process(S, dst);
  shuso_ipc_lua_data_t  *data;
  if(top >= 3 && lua_isstring(L, 2) && !lua_isfunction(L, 3) && !lua_isthread(L, 3)) { //3 args means we got the dst, the message name, and the Lua unpacked data
    const char *name = luaL_checkstring(L, 2);
    data = luaS_lua_ipc_pack_data(L, 3, name, !shuso_processes_share_heap(S, S->procnum, dst));
    data->sender = S->procnum;
  }
  else if(top == 2 || (top >= 3 && lua_isnil(L, 3))) { //2 args means we got dst, pre-packed data (which includes the message name)
    luaL_checktype(L, 2, LUA_TLIGHTUSERDATA);
    data = (void *)lua_topointer(L, 2);
  }
  else if(top < 2) {
    return luaL_error(L, "too few arguments");
  }
  
  const char *err;
  if(!shuso_procnum_valid(S, dst, &err)) {
    return luaL_error(L, "%s", err);
  }
  
  lua_getfield(L, LUA_REGISTRYINDEX, "shuttlesock.lua_ipc.code");
  
  int ipc_code = lua_tointeger(L, -1);
  assert(ipc_code > 0);
  lua_pop(L, 1);
  
  
  if(lua_isfunction(L, -1) || lua_isthread(L, -1)) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, data->reftable);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "caller");
    lua_pop(L, 1);
  }
  else if(yield) {
    assert(lua_isyieldable(L));
    lua_rawgeti(L, LUA_REGISTRYINDEX, data->reftable);
    lua_pushthread(L);
    lua_setfield(L, -2, "caller");
    lua_pop(L, 1);
  }
  
  shuso_ipc_send(S, dst_proc, ipc_code, data);
  
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
