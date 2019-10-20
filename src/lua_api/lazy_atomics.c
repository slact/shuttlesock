#include <shuttlesock.h>
#include <lauxlib.h>

typedef struct {
  size_t sz;
  char   data[];
} sized_string_t;

#define SHUSO_LUA_SHATOMIC_NIL      0
#define SHUSO_LUA_SHATOMIC_BOOLEAN  1
#define SHUSO_LUA_SHATOMIC_STRING   2
#define SHUSO_LUA_SHATOMIC_INTEGER  3
#define SHUSO_LUA_SHATOMIC_NUMBER   4
#define SHUSO_LUA_SHATOMIC_POINTER  5

typedef struct {
  _Atomic      unsigned typemix; //update counter and type enum in one atomic bundle
  _Atomic     (sized_string_t *) string;
  _Atomic      int      integer;
  _Atomic      double   number;
  _Atomic      bool     boolean;
  _Atomic     (void *)  pointer;
} shuso_lua_lazy_atomic_value_t;


typedef struct {
  shuso_lua_lazy_atomic_value_t *atomic;
} lua_lazy_atomic_userdata_t;

static bool update_type_safely(shuso_lua_lazy_atomic_value_t *atomic, unsigned old_typemix, unsigned new_type) {
  unsigned new_typemix = (((old_typemix >> 8) + 1)<<8) + new_type;
  return atomic_compare_exchange_strong(&atomic->typemix, &old_typemix, new_typemix);
}

static int get_type(shuso_lua_lazy_atomic_value_t *atomic) {
  return atomic->typemix & 0x0F;
}

static int Lua_lazy_atomics_value_set(lua_State *L);

static int Lua_lazy_atomics_value_create(lua_State *L) {
  shuso_t *S = shuso_state(L);
  shuso_lua_lazy_atomic_value_t *atomicval = shuso_shared_slab_alloc(&S->common->shm, sizeof(*atomicval));
  int nargs = lua_gettop(L);
  if(!atomicval) {
    lua_pushnil(L);
    lua_pushstring(L, "failed to allocate Lua shared atomic value");
    return 2;
  }
  
  lua_lazy_atomic_userdata_t *ud = lua_newuserdata(L, sizeof(*ud));
  if(!ud) {
    lua_pushnil(L);
    lua_pushstring(L, "failed to allocate Lua shared atomic value userdata");
    return 2;
  }
  
  ud->atomic = atomicval;
  luaL_setmetatable(L, "shuttlesock.lazy_atomic");
  
  if(nargs == 0) {
    atomicval->typemix = SHUSO_LUA_SHATOMIC_NIL;
  }
  else {
    lua_pushcfunction(L, Lua_lazy_atomics_value_set);
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    luaS_call(L, 2, 0);
  }
  
  return 1;
}

static int Lua_lazy_atomics_value_destroy(lua_State *L) {
  //this is not concurrency-safe! destruction assumes the value is no longer in use
  shuso_t *S = shuso_state(L);
  lua_lazy_atomic_userdata_t *ud = luaL_checkudata(L, 1, "shuttlesock.lazy_atomic");
  shuso_lua_lazy_atomic_value_t *atomicval = ud->atomic;
  if(atomicval->string) {
    shuso_shared_slab_free(&S->common->shm, atomicval->string);
    atomicval->string = NULL;
  }
  shuso_shared_slab_free(&S->common->shm, atomicval);
  lua_pushboolean(L, 1);
  return 1;
}


static int Lua_lazy_atomics_value_set(lua_State *L) {
  shuso_t *S = shuso_state(L);
  lua_lazy_atomic_userdata_t      *ud = luaL_checkudata(L, 1, "shuttlesock.lazy_atomic");
  shuso_lua_lazy_atomic_value_t   *atomicval = ud->atomic;
  if(atomicval == NULL) {
    lua_pushnil(L);
    lua_pushliteral(L, "can't set destroyed atomic value");
    return 2;
  }
  luaL_checkany(L, 2);
  int type = lua_type(L, 2);
  
  unsigned old_typemix = atomicval->typemix;
  unsigned new_atype;
  switch(type) {
    case LUA_TNUMBER: {
      if(lua_isinteger(L, 2)) {
        int integer = lua_tointeger(L, 2);
        atomicval->integer = integer;
        new_atype = SHUSO_LUA_SHATOMIC_INTEGER;
      }
      else {
        lua_Number num = lua_tonumber(L, 2);
        atomicval->number = num;
        new_atype = SHUSO_LUA_SHATOMIC_NUMBER;
      }
      break;
    }
    case LUA_TSTRING: {
      size_t sz;
      const char *str = lua_tolstring(L, 2, &sz);
      sized_string_t *shared_str, *old_shared_str;
      shared_str = shuso_shared_slab_alloc(&S->common->shm, sizeof(*shared_str) + sz);
      if(!sz) {
        lua_pushnil(L);
        lua_pushliteral(L, "failed to allocate shared string for atomic value");
        return 2;
      }
      shared_str->sz = sz;
      memcpy(shared_str->data, str, sz);
      old_shared_str = atomic_exchange(&atomicval->string, shared_str);
      new_atype = SHUSO_LUA_SHATOMIC_STRING;
      if(old_shared_str != NULL) {
        shuso_shared_slab_free(&S->common->shm, old_shared_str);
      }
      break;
    }
    case LUA_TLIGHTUSERDATA: {
      atomicval->pointer = (void *)lua_topointer(L, 2);
      new_atype = SHUSO_LUA_SHATOMIC_POINTER;
      break;
    }
    case LUA_TNIL: {
      new_atype = SHUSO_LUA_SHATOMIC_NIL;
      break;
    }
    default: {
      lua_pushnil(L);
      lua_pushfstring(L, "can't set atomic value type %s", lua_typename(L, type));
      return 2;
    }
  }
  
  bool newval_ok = update_type_safely(atomicval, old_typemix, new_atype);
  
  lua_pushboolean(L, newval_ok);
  return 1;
}

static int Lua_lazy_atomics_value_get(lua_State *L) {
  lua_lazy_atomic_userdata_t      *ud = luaL_checkudata(L, 1, "shuttlesock.lazy_atomic");
  shuso_lua_lazy_atomic_value_t   *atomicval = ud->atomic;
  if(atomicval == NULL) {
    lua_pushnil(L);
    lua_pushliteral(L, "can't set destroyed atomic value");
    return 2;
  }
  
  // we can afford to not wrap this in a Check-And-Set for the type
  // because the value_set operation does not erase the previous value
  // when the type changes
  // so even if the type value is stale, the stale data is still present
  int valtype = get_type(atomicval);
  switch(valtype) {
    case SHUSO_LUA_SHATOMIC_NIL:
      lua_pushnil(L);
      return 1;
    case SHUSO_LUA_SHATOMIC_BOOLEAN:
      lua_pushboolean(L, atomicval->boolean);
      return 1;
    case SHUSO_LUA_SHATOMIC_INTEGER:
      lua_pushinteger(L, atomicval->integer);
      return 1;
    case SHUSO_LUA_SHATOMIC_NUMBER:
      lua_pushnumber(L, atomicval->number);
      return 1;
    case SHUSO_LUA_SHATOMIC_POINTER:
      lua_pushlightuserdata(L, atomicval->pointer);
      return 1;
    case SHUSO_LUA_SHATOMIC_STRING: {
      sized_string_t *str = atomicval->string;
      lua_pushlstring(L, str->data, str->sz);
      return 1;
    }
    default: 
      lua_pushnil(L);
      lua_pushliteral(L, "invalid atomic value type");
      return 2;
  }
}

int Lua_lazy_atomics_value_increment(lua_State *L) {
  lua_lazy_atomic_userdata_t      *ud = luaL_checkudata(L, 1, "shuttlesock.lazy_atomic");
  shuso_lua_lazy_atomic_value_t   *atomicval = ud->atomic;
  if(atomicval == NULL) {
    lua_pushnil(L);
    lua_pushliteral(L, "can't set destroyed atomic value");
    return 2;
  }
  luaL_checktype(L, 2, LUA_TNUMBER);
  int type = get_type(atomicval);
  switch(type) {
    case SHUSO_LUA_SHATOMIC_INTEGER: {
      int old = atomic_fetch_add(&atomicval->integer, lua_tointeger(L, 2));
      lua_pushinteger(L, old);
      return old;
    }
    case SHUSO_LUA_SHATOMIC_NUMBER:
      lua_pushnil(L);
      lua_pushliteral(L, "can't increment atomic floating-point number");
      return 2;
     case SHUSO_LUA_SHATOMIC_BOOLEAN:
      lua_pushnil(L);
      lua_pushliteral(L, "can't increment atomic boolean");
      return 2;
    case SHUSO_LUA_SHATOMIC_NIL:
      lua_pushnil(L);
      lua_pushliteral(L, "can't increment atomic nil");
      return 2;
    case SHUSO_LUA_SHATOMIC_POINTER:
      lua_pushnil(L);
      lua_pushliteral(L, "can't increment atomic pointer");
      return 2;
    case SHUSO_LUA_SHATOMIC_STRING:
      lua_pushnil(L);
      lua_pushliteral(L, "can't increment atomic string");
      return 2;
  }
  lua_pushnil(L);
  lua_pushliteral(L, "invalid atomic value type");
  return 2;
}

luaL_Reg shuttlesock_lua_lazy_atomics_table[] = {
  {"create", Lua_lazy_atomics_value_create},
  {"destroy", Lua_lazy_atomics_value_destroy},
  {NULL, NULL}
};

static int Lua_lazy_atomics_value_gxcopy_save(lua_State *L) {
  lua_lazy_atomic_userdata_t      *ud = luaL_checkudata(L, 1, "shuttlesock.lazy_atomic");
  lua_pushlightuserdata(L, ud->atomic);
  return 1;
}

static int Lua_lazy_atomics_value_gxcopy_load(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  shuso_lua_lazy_atomic_value_t   *atomicval = (void *)lua_topointer(L, 1);
  lua_lazy_atomic_userdata_t *ud = lua_newuserdata(L, sizeof(*ud));
  if(!ud) {
    return luaL_error(L, "failed to allocate Lua shared atomic value userdata");
  }
  
  ud->atomic = atomicval;
  luaL_setmetatable(L, "shuttlesock.lazy_atomic");
  return 1;
}

int luaS_push_lazy_atomics_module(lua_State *L) {
  luaL_newlib(L, shuttlesock_lua_lazy_atomics_table);
  
  if(luaL_newmetatable(L, "shuttlesock.lazy_atomic")) {
    //__index table
    luaL_getsubtable(L, -1, "__index");
    luaL_setfuncs(L, (luaL_Reg[]){
      {"set", Lua_lazy_atomics_value_set},
      {"value", Lua_lazy_atomics_value_get},
      {"increment", Lua_lazy_atomics_value_increment},
      {NULL, NULL}
    }, 0);
    lua_pop(L, 1);
    
    luaL_setfuncs(L, (luaL_Reg[]){
      {"__gxcopy_save", Lua_lazy_atomics_value_gxcopy_save},
      {"__gxcopy_load", Lua_lazy_atomics_value_gxcopy_load},
      {NULL, NULL}
    }, 0);
  }
  lua_pop(L, 1);
  
  return 1;
}
