#include <shuttlesock.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <shuttlesock/embedded_lua_scripts.h>

#include <glob.h>

#if defined(SHUTTLESOCK_DEBUG_SANITIZE) || defined(SHUTTLESOCK_DEBUG_VALGRIND) || defined(__clang_analyzer__)
#define INIT_LUA_ALLOCS 1
#endif

static bool function_result_ok(lua_State *L, bool preserve_result) {
  if(lua_isnil(L, -2)) {
    shuso_t *S = shuso_state(L);
    if(!lua_isstring(L, -1)) {
      shuso_set_error(S, "lua function returned nil with no error message");      
    }
    else {
      const char *errstr = lua_tostring(L, -1);
      shuso_set_error(S, "%s", errstr);
    }
    //luaS_printstack(L);
    //raise(SIGABRT);
    lua_pop(L, 2);
    return false;
  }
  //bool ret = lua_toboolean(L, -2);
  if(preserve_result) {
    lua_pop(L, 1); //just pop the nil standin for the error
  }
  else {
    lua_pop(L, 2);
  }
  //return ret;
  return true;
}

bool luaS_function_call_result_ok(lua_State *L, int nargs, bool preserve_result) {
  luaS_call(L, nargs, 2);
  return function_result_ok(L, preserve_result);
}

bool luaS_function_pcall_result_ok(lua_State *L, int nargs, bool preserve_result) {
  int rc = lua_pcall(L, nargs, 2, 0);
  if(rc != LUA_OK) {
    shuso_set_error(shuso_state(L), lua_tostring(L, -1));
    lua_pop(L, 1);
    return false;
  }
  return function_result_ok(L, preserve_result);
}

static int luaS_traceback(lua_State *L) {
  if (!lua_isstring(L, -1)) { /* 'message' not a string? */
    return 1;  /* keep it intact */
  }

  lua_getglobal(L, "debug");
  lua_getfield(L, -1, "traceback");
  lua_remove(L, -2);

  lua_pushvalue(L, 1);  /* pass error message */
  lua_pushinteger(L, 2);  /* skip this function and traceback */
  lua_call(L, 2, 1);  /* call debug.traceback */
  return 1;
}

void luaS_call(lua_State *L, int nargs, int nresults) {
  int rc;
  if(!lua_isfunction(L, -(nargs+1))) {
    luaS_printstack(L);
    shuso_log_error(shuso_state(L), "nargs: %i", nargs);
    assert(lua_isfunction(L, -(nargs+1)));
  }
  
  lua_pushcfunction(L, luaS_traceback);
  lua_insert(L, 1);
  
  rc = lua_pcall(L, nargs, nresults, 1);
  if (rc != 0) {
    shuso_log_error(shuso_state(L), "Lua error: %s", lua_tostring(L, -1));
    lua_pop(L, 1);
    lua_gc(L, LUA_GCCOLLECT, 0);
//#ifdef SHUTTLESOCK_CRASH_ON_LUA_ERROR
    raise(SIGABRT);
//#endif
  }
  lua_remove(L, 1);
}

char *luaS_dbgval(lua_State *L, int n) {
  static char buf[255];
  int         type = lua_type(L, n);
  const char *typename = lua_typename(L, type);
  const char *str;
  lua_Number  num;
  
  
  switch(type) {
    case LUA_TNUMBER:
      num = lua_tonumber(L, n);
      sprintf(buf, "%s: %f", typename, num);
      break;
    case LUA_TBOOLEAN:
      sprintf(buf, "%s: %s", typename, lua_toboolean(L, n) ? "true" : "false");
      break;
    case LUA_TSTRING:
      str = lua_tostring(L, n);
      sprintf(buf, "%s: %.50s%s", typename, str, strlen(str) > 50 ? "..." : "");
      break;
    default:
      lua_getglobal(L, "tostring");
      lua_pushvalue(L, n);
      lua_call(L, 1, 1);
      str = lua_tostring(L, -1);
      sprintf(buf, "%s", str);
      lua_pop(L, 1);
  }
  return buf;
}
void luaS_printstack(lua_State *L) {
  int        top = lua_gettop(L);
  shuso_t   *S = shuso_state(L);
  shuso_log_warning(S, "lua stack:");
  for(int n=top; n>0; n--) {
    shuso_log_warning(S, "  [%-2i  %i]: %s", -(top-n+1), n, luaS_dbgval(L, n));
  }
}

void luaS_mm(lua_State *L, int stack_index) {
  int absindex = lua_absindex(L, stack_index);
  lua_getglobal(L, "require");
  lua_pushliteral(L, "mm");
  lua_call(L, 1, 1);
  lua_pushvalue(L, absindex);
  lua_call(L, 1, 0);
}

void luaS_push_inspect_string(lua_State *L, int stack_index) {
  int absindex = lua_absindex(L, stack_index);
  lua_getglobal(L, "require");
  lua_pushliteral(L, "inspect");
  lua_call(L, 1, 1);
  lua_pushvalue(L, absindex);
  lua_call(L, 1, 1);
}

void luaS_inspect(lua_State *L, int stack_index) {
  luaS_push_inspect_string(L, stack_index);
  
  lua_getglobal(L, "print");
  lua_pushvalue(L, -2);
  lua_remove(L, -3);
  lua_call(L, 1, 0);
}

int luaS_glob(lua_State *L) {
  const char *pattern = luaL_checkstring(L, 1);
  int         rc = 0;
  glob_t      globres;
  rc = glob(pattern, GLOB_ERR | GLOB_MARK, NULL, &globres);
  lua_remove(L, 1);
  switch(rc) {
    case 0:
      lua_newtable(L);
      for(unsigned i=1; i<=globres.gl_pathc; i++) {
        lua_pushstring(L, globres.gl_pathv[i-1]);
        lua_rawseti(L, -2, i);
      }
      globfree(&globres);
      return 1;
      
    case GLOB_NOSPACE:
      lua_pushnil(L);
      lua_pushliteral(L, "not enough memory to glob");
      return 2;
      
    case GLOB_ABORTED:
      lua_pushnil(L);
      lua_pushliteral(L, "read error while globbing");
      return 2;
      
    case GLOB_NOMATCH:
      lua_newtable(L);
      return 1;
      
    default:
      lua_pushnil(L);
      lua_pushliteral(L, "weird error while globbing");
      return 2;
  }
}

#ifdef INIT_LUA_ALLOCS
static void *initializing_allocator(void *ud, void *ptr, size_t osize,
 size_t nsize) {

  //printf("ptr: %p, osz: %d, nsz: %d\n", ptr, (int)osize, (int)nsize);
  (void)ud;
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }

  else {
  void *nptr = realloc(ptr, nsize);
    if(!ptr) {
      memset(nptr, '0', nsize);
    }
    else if(nsize > osize) {
      memset((char *)nptr+(nsize - osize), '0', nsize - (nsize - osize));
    }
    return nptr;
  }
}
#endif

bool shuso_lua_create(shuso_t *S) {
  if(S->procnum == SHUTTLESOCK_MASTER) {
    assert(S->lua.state == NULL);
  }
#ifndef INIT_LUA_ALLOCS
  if((S->lua.state = luaL_newstate()) == NULL) {
    return false;
  }
#else
  if((S->lua.state = lua_newstate(initializing_allocator, NULL)) == NULL) {
    return false;
  }
#endif
  luaL_openlibs(S->lua.state);
  //initialize shuttlesocky lua env
  
  return true;
}

int luaS_do_embedded_script(lua_State *L) {
  const char *name = luaL_checkstring(L, -1);
  shuso_lua_embedded_scripts_t *script;
  for(script = &shuttlesock_lua_embedded_scripts[0]; script->name != NULL; script++) {
    if(strcmp(script->name, name) == 0) {
      int rc;
      lua_pushfstring(L, "@%s", script->filename);
      int fname_idx = lua_gettop(L);
      if(script->compiled) {
        rc = luaL_loadbufferx(L, script->compiled, script->compiled_len, lua_tostring(L, -1), "b");
      }
      else {
        rc = luaL_loadbuffer(L, script->script, script->script_len, lua_tostring(L, -1));
      }
      lua_remove(L, fname_idx);
      if(rc != LUA_OK) {
        lua_error(L);
        return 0;
      }
      
      lua_call(L, 0, 1);
      return 1;
    }
  }
  luaL_error(L, "embedded script %s not found", name);
  return 0;
}

shuso_t *shuso_state_from_lua(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "shuttlesock.userdata");
  assert(lua_islightuserdata(L, -1));
  shuso_t *S = (shuso_t *)lua_topointer(L, -1);
  lua_pop(L, 1);
  return S;
}

bool luaS_set_shuttlesock_state_pointer(lua_State *L, shuso_t *S) {
  lua_pushlightuserdata(L, S);
  lua_setfield(L, LUA_REGISTRYINDEX, "shuttlesock.userdata");
  return true;
}

bool shuso_lua_initialize(shuso_t *S) {
  lua_State *L = S->lua.state;
  luaS_set_shuttlesock_state_pointer(L, S);
  
  luaL_requiref(L, "shuttlesock.core", luaS_push_core_module, 0);
  lua_pop(L, 1);
  
  luaL_requiref(L, "shuttlesock.system", luaS_push_system_module, 0);
  lua_pop(L, 1);
  
  for(shuso_lua_embedded_scripts_t *script = &shuttlesock_lua_embedded_scripts[0]; script->name != NULL; script++) {
    if(script->module) {
      luaL_requiref(L, script->name, luaS_do_embedded_script, 0);
      lua_pop(L, 1);
    }
  }
  
  //lua doesn't come with a glob, and config needs it when including files
  lua_getglobal(L, "require");
  lua_pushliteral(L, "shuttlesock.config");
  lua_call(L, 1, 1);
  
  S->config.index = luaL_ref(L, LUA_REGISTRYINDEX);
  
  return true;
}

bool shuso_lua_destroy(shuso_t *S) {
  assert(!S->lua.external);
  assert(S->lua.state != NULL);
  lua_close(S->lua.state);
  S->lua.state = NULL;
  return true;
}

int luaS_resume(lua_State *thread, lua_State *from, int nargs) {
  int          rc;
  const char  *errmsg;
  shuso_t     *S;
  rc = lua_resume(thread, from, nargs);
  switch(rc) {
    case LUA_OK:
    case LUA_YIELD:
      break;
    default:
      S = shuso_state(thread);
      errmsg = lua_tostring(thread, -1);
      luaL_traceback(thread, thread, errmsg, 1);
      shuso_log_error(S, "lua coroutine error: %s", lua_tostring(thread, -1));
      lua_pop(thread, 1);
      lua_gc(thread, LUA_GCCOLLECT, 0);
      break;
  }
  return rc;
}

int luaS_call_or_resume(lua_State *L, int nargs) {
  int         state_or_func_index = -1 - nargs;
  int         type = lua_type(L, state_or_func_index);
  lua_State  *coro;
  switch(type) {
    case LUA_TFUNCTION:
      lua_call(L, nargs, 0);
      return 0;
    case LUA_TTHREAD:
      coro = lua_tothread(L, state_or_func_index);
      lua_xmove(L, coro, nargs);
      lua_resume(coro, L, nargs);
      return 0;
    default:
      return luaL_error(L, "attempted to call-or-resume something that's not a function or coroutine");
  }
}

int luaS_shuso_error(lua_State *L) {
  shuso_t *S = shuso_state(L);
  const char *errmsg = shuso_last_error(S);
  return luaL_error(L, "%s", errmsg == NULL ? "(unknown error)" : errmsg);
}


static int lua_function_dump_writer (lua_State *L, const void *b, size_t size, void *B) {
  luaL_addlstring((luaL_Buffer *) B, (const char *)b, size);
  return 0;
}
int luaS_function_dump(lua_State *L) {
  luaL_Buffer b;
  luaL_checktype(L, -1, LUA_TFUNCTION);
  luaL_buffinit(L,&b);
  if (lua_dump(L, lua_function_dump_writer, &b, 0) != 0)
    return luaL_error(L, "unable to dump given function");
  luaL_pushresult(&b);
  return 1;
}

typedef struct {
  struct {
    lua_State *state;
    int        copies_ref;
  }          src;
  struct {
    lua_State *state;
    int        copies_ref;
  }          dst;
} gxcopy_state_t;


static bool gxcopy_any(gxcopy_state_t *gxs);

static bool gxcopy_cache_load(gxcopy_state_t *gxs) {
  lua_State *Ls = gxs->src.state, *Ld = gxs->dst.state;
  luaL_checkstack(Ls, 2, NULL);
  luaL_checkstack(Ld, 2, NULL);
  
  //is this thing cached?
  lua_rawgeti(Ls, LUA_REGISTRYINDEX, gxs->src.copies_ref);
  lua_pushvalue(Ls, -2);
  lua_gettable(Ls, -2);
  if(lua_isnumber(Ls, -1)) {
    int index = lua_tonumber(Ls, -1);
    lua_pop(Ls, 2);
    
    lua_rawgeti(Ld, LUA_REGISTRYINDEX, gxs->dst.copies_ref);
    lua_rawgeti(Ld, -1, index);
    lua_remove(Ld, -2);
    return true;
  }
  lua_pop(Ls, 2);
  return false;
}

static bool gxcopy_cache_store(gxcopy_state_t *gxs) {
  lua_State *Ls = gxs->src.state, *Ld = gxs->dst.state;
  luaL_checkstack(Ls, 3, NULL);
  luaL_checkstack(Ld, 2, NULL);
  
   //cache copied thing in dst state
  lua_rawgeti(Ld, LUA_REGISTRYINDEX, gxs->dst.copies_ref);
  lua_pushvalue(Ld, -2);
  int ref = luaL_ref(Ld, -2);
  lua_pop(Ld, 1);
  
  //cache copied thing ref in src state
  lua_rawgeti(Ls, LUA_REGISTRYINDEX, gxs->src.copies_ref);
  lua_pushvalue(Ls, -2);
  lua_pushinteger(Ls, ref);
  lua_rawset(Ls, -3);
  lua_pop(Ls, 1);
  return true;
}

static bool gxcopy_metatable(gxcopy_state_t *gxs) {
  lua_State *Ls = gxs->src.state, *Ld = gxs->dst.state;
  luaL_checkstack(Ls, 3, NULL);
  luaL_checkstack(Ld, 3, NULL);
  
  assert(lua_istable(Ls, -1));
  if(!lua_getmetatable(Ls, -1)) {
    return true;
  }
  
  if(lua_getfield(Ls, -1, "__gxcopy") == LUA_TTABLE) {
    const char *module_name = NULL;
    const char *module_key = NULL;
    
    if(lua_getfield(Ls, -1, "module") == LUA_TSTRING) {
      module_name = lua_tostring(Ls, -1);
    }
    lua_pop(Ls, 1);
    
    if(lua_getfield(Ls, -1, "module_key") == LUA_TSTRING) {
      module_key = lua_tostring(Ls, -1);
    }
    lua_pop(Ls, 1);
    
    lua_pop(Ls, 2); //pop __gxcopy and metatable
    
    if(!module_key || !module_name) {
      return shuso_set_error(shuso_state(Ls), "failed to gxcopy metatable, __gxcopy missing 'module' or 'module_key' field");
    }
    
    lua_getglobal(Ld, "require");
    lua_pushstring(Ld, module_name);
    if(lua_pcall(Ld, 1, 1, 0) != LUA_OK) {
      return shuso_set_error(shuso_state(Ls), "failed to gxcopy metatable, __gxcopy 'module' %s is missing", module_name);
    }
    if(!lua_istable(Ld, -1)) {
      return shuso_set_error(shuso_state(Ls), "failed to gxcopy metatable, __gxcopy 'module' %s is not a table", module_name);
    }
    if(lua_getfield(Ld, -1, module_key) != LUA_TTABLE) {
      return shuso_set_error(shuso_state(Ls), "failed to gxcopy metatable, __gxcopy 'module' %s 'module_key' \"%s\" is not a table", module_name, module_key);
    }
    lua_setmetatable(Ld, -3);
    lua_pop(Ld, 1);
    return true;
  }
  lua_pop(Ls, 1);
  
  if(!gxcopy_any(gxs)) {
    return false;
  }
  lua_pop(Ls, 1);
  lua_setmetatable(Ld, -2);
  
  return true;
}

static bool gxcopy_table(gxcopy_state_t *gxs) {
  lua_State *Ls = gxs->src.state, *Ld = gxs->dst.state;
  luaL_checkstack(Ls, 4, NULL);
  luaL_checkstack(Ld, 1, NULL);
  
  int tindex = lua_absindex(Ls, -1);
  lua_rawgeti(Ls, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
  if(lua_compare(Ls, -1, -2, LUA_OPEQ)) {
    //it's the globals table
    lua_pop(Ls, 1);
    lua_rawgeti(Ld, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    return true;
  }
  lua_pop(Ls, 1);
  
  if(gxcopy_cache_load(gxs)) {
    return true;
  }
  lua_newtable(Ld);
  gxcopy_cache_store(gxs);
  
  lua_pushnil(Ls);  /* first key */
  while(lua_next(Ls, tindex) != 0) {
    //key at -2, value at -1
    lua_pushvalue(Ls, -2);
    gxcopy_any(gxs); //copy key
    lua_pop(Ls, 1);
    gxcopy_any(gxs); //copy value
    lua_pop(Ls, 1);
    lua_rawset(Ld, -3);
  }
  if(!gxcopy_metatable(gxs)) {
    return false;
  }
  return true;
}

static bool gxcopy_upvalues(gxcopy_state_t *gxs, int nups) {
  lua_State *Ls = gxs->src.state;
  luaL_checkstack(Ls, 1, NULL);
  for(int i=1; i<=nups; i++) {
    assert(lua_getupvalue(Ls, -1, i));
    if(!gxcopy_any(gxs)) {
      return false;
    }
    lua_pop(Ls, 1);
  }
  return true;
}

static bool gxcopy_function(gxcopy_state_t *gxs) {
  lua_State *Ls = gxs->src.state, *Ld = gxs->dst.state;
  luaL_checkstack(Ls, 2, NULL);
  luaL_checkstack(Ld, 2, NULL);
  if(gxcopy_cache_load(gxs)) {
    return true;
  }
  
  lua_Debug dbg;
  lua_pushvalue(Ls, -1);
  lua_getinfo(Ls, ">Snlu", &dbg);
  if(!dbg.name) dbg.name = "";

  if(lua_iscfunction(Ls, -1)) {
    //this one's easy;
    lua_CFunction func = lua_tocfunction(Ls, -1);
    if(dbg.nups > 0) {
      if(!gxcopy_upvalues(gxs, dbg.nups)) {
        return false;
      }
      lua_pushcclosure(Ld, func, dbg.nups);
    }
    else {
      lua_pushcfunction(Ld, func);
    }
    gxcopy_cache_store(gxs);
    return true;
  }
  
  luaS_function_dump(Ls);
  size_t sz;
  const char *buf = lua_tolstring(Ls, -1, &sz);
  
  
  if(luaL_loadbufferx(Ld, buf, sz, dbg.name, "b") != LUA_OK) {
    return shuso_set_error(shuso_state(Ls), "failed to gxcopy function %s, loadbufferx failed", dbg.name);
  }
  lua_pop(Ls, 1);
  assert(lua_isfunction(Ld, -1));
  
  int funcidx = lua_absindex(Ld, -1);
  if(dbg.nups > 0) {
    if(!gxcopy_upvalues(gxs, dbg.nups)) {
      return false; 
    }
    for(int i=dbg.nups; i>0; i--) {
      if(!lua_setupvalue(Ld, funcidx, i)) {
        return shuso_set_error(shuso_state(Ls), "failed to gxcopy function %s, setupvalue %i failed", dbg.name, i);
      }
    }
  }
  
  gxcopy_cache_store(gxs);
  
  return true;
}

static bool gxcopy_userdata(gxcopy_state_t *gxs) {
  return shuso_set_error(shuso_state(gxs->src.state), "failed to gxcopy userdata, don'tknow how to do that yet");

}
static bool gxcopy_thread(gxcopy_state_t *gxs) {
  return shuso_set_error(shuso_state(gxs->src.state), "failed to gxcopy coroutine, this isn't possible to do between different Lua states");
}


static bool gxcopy_any(gxcopy_state_t *gxs) {
  lua_State *Ls = gxs->src.state, *Ld = gxs->dst.state;
  int type = lua_type(Ls, -1);
  switch(type) {
    case LUA_TNIL:
      lua_pushnil(Ld);
      break;
    case LUA_TNUMBER:
      luaL_checkstack(Ld, 1, NULL);
      if(lua_isinteger(Ls, -1)) {
        lua_pushinteger(Ld, lua_tointeger(Ls, -1));
      }
      else {
        lua_pushnumber(Ld, lua_tonumber(Ls, -1));
      }
      break;
    case LUA_TBOOLEAN:
      luaL_checkstack(Ld, 1, NULL);
      lua_pushboolean(Ld, lua_toboolean(Ls, -1));
      break;
    case LUA_TSTRING: {
      size_t sz;
      const char *str = lua_tolstring(Ls, -1, &sz);
      luaL_checkstack(Ld, 1, NULL);
      lua_pushlstring(Ld, str, sz);
    } break;
    case LUA_TTABLE:
      if(!gxcopy_table(gxs)) {
        return false;
      }
      break;
    case LUA_TFUNCTION:
      if(!gxcopy_function(gxs)) {
        return false;
      }
      break;
    case LUA_TUSERDATA:
      if(!gxcopy_userdata(gxs)) {
        return false;
      }
      break;
    case LUA_TTHREAD:
      if(!gxcopy_thread(gxs)) {
        return false;
      }
      break;
    case LUA_TLIGHTUSERDATA:
      luaL_checkstack(Ld, 1, NULL);
      lua_pushlightuserdata(Ld, (void *)lua_topointer(Ls, -1));
      break;
  }
  return true;
}

//copy value from one global state to another
bool luaS_gxcopy(lua_State *Ls, lua_State *Ld) {
  
  lua_newtable(Ls);
  int src_copies_ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
  
  lua_newtable(Ld);
  int dst_copies_ref = luaL_ref(Ld, LUA_REGISTRYINDEX);
  
  gxcopy_state_t gxs = {
    .src.state = Ls,
    .src.copies_ref = src_copies_ref,
    .dst.state = Ld,
    .dst.copies_ref = dst_copies_ref,
  };
  
  bool ok = gxcopy_any(&gxs);
  luaL_unref(Ls, LUA_REGISTRYINDEX, src_copies_ref);
  luaL_unref(Ld, LUA_REGISTRYINDEX, dst_copies_ref);
  return ok;
}


