#include <shuttlesock.h>
#include <lauxlib.h>

typedef struct {
  size_t sz;
  char   data[];
} sized_string_t;

typedef struct {
  _Atomic enum {
    SHUSO_LUA_SHATOMIC_NIL = 0,
    SHUSO_LUA_SHATOMIC_BOOLEAN,
    SHUSO_LUA_SHATOMIC_STRING,
    SHUSO_LUA_SHATOMIC_INTEGER,
    SHUSO_LUA_SHATOMIC_NUMBER,
    SHUSO_LUA_SHATOMIC_POINTER
  }                     type;
  _Atomic     (sized_string_t *) string;
  _Atomic      int      integer;
  _Atomic      double   number;
  _Atomic      bool     boolean;
  _Atomic     (void *)  pointer;
} shuso_lua_lazy_atomic_value_t;

static int Lua_lazy_atomics_value_set(lua_State *L);

static int Lua_lazy_atomics_value_create(lua_State *L) {
  shuso_t *S = shuso_state(L);
  shuso_lua_lazy_atomic_value_t *atomicval = shuso_shared_slab_alloc(&S->common->shm, sizeof(*atomicval));
  
  if(!atomicval) {
    lua_pushnil(L);
    lua_pushstring(L, "failed to allocate Lua shared atomic value");
    return 2;
  }
  
  if(lua_gettop(L) == 0) {
    atomicval->type = SHUSO_LUA_SHATOMIC_NIL;
  }
  else {
    lua_pushcfunction(L, Lua_lazy_atomics_value_set);
    lua_pushlightuserdata(L, atomicval);
    lua_pushvalue(L, 1);
    luaS_call(L, 2, 0);
  }
  
  lua_pushlightuserdata(L, atomicval);
  return 1;
}

static int Lua_lazy_atomics_value_destroy(lua_State *L) {
  //this is not concurrency-safe! destruction assumes the value is no longer in use
  shuso_t *S = shuso_state(L);
  shuso_lua_lazy_atomic_value_t *atomicval = (void *)lua_topointer(L, 1);
  atomicval->type = SHUSO_LUA_SHATOMIC_NIL;
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
  shuso_lua_lazy_atomic_value_t *atomicval = (void *)lua_topointer(L, 1);
  
  int type = lua_type(L, 1);
  switch(type) {
    case LUA_TNUMBER: {
      if(lua_isinteger(L, 1)) {
        int integer = lua_tointeger(L, 1);
        atomicval->integer = integer;
        atomicval->type = SHUSO_LUA_SHATOMIC_INTEGER;
      }
      else {
        lua_Number num = lua_tonumber(L, 1);
        atomicval->number = num;
        atomicval->type = SHUSO_LUA_SHATOMIC_NUMBER;
      }
      break;
    }
    case LUA_TSTRING: {
      size_t sz;
      const char *str = lua_tolstring(L, 1, &sz);
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
      if(old_shared_str != NULL) {
        shuso_shared_slab_free(&S->common->shm, old_shared_str);
      }
      break;
    }
    case LUA_TLIGHTUSERDATA: {
      atomicval->pointer = (void *)lua_topointer(L, 1);
      atomicval->type = SHUSO_LUA_SHATOMIC_POINTER;
      break;
    }
    case LUA_TNIL: {
      atomicval->type = SHUSO_LUA_SHATOMIC_NIL;
      break;
    }
    default: {
      lua_pushnil(L);
      lua_pushfstring(L, "can't set atomic value type %s", lua_typename(L, type));
      return 2;
    }
  }
  
  lua_pushboolean(L, 1);
  return 1;
}

static int Lua_lazy_atomics_value_get(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  shuso_lua_lazy_atomic_value_t *atomicval = (void *)lua_topointer(L, 1);
  // we can afford to not wrap this in a Check-And-Set for the type
  // because the value_set operation does not erase the previous value
  // when the type changes
  // so even if the type value is stale, the stale data is still present
  int valtype = atomicval->type;
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
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  luaL_checktype(L, 2, LUA_TNUMBER);
  shuso_lua_lazy_atomic_value_t *atomicval = (void *)lua_topointer(L, 1);
  int type = atomicval->type;
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
  
  {"set", Lua_lazy_atomics_value_set},
  {"get", Lua_lazy_atomics_value_get},
  {"increment", Lua_lazy_atomics_value_increment},
  {NULL, NULL}
};


int luaS_push_lazy_atomics_module(lua_State *L) {
  luaL_newlib(L, shuttlesock_lua_lazy_atomics_table);
  return 1;
}
