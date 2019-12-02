#include <shuttlesock.h>
#include <shuttlesock/embedded_lua_scripts.h>
#include <shuttlesock/modules/lua_bridge/api/lazy_atomics.h>
#include <shuttlesock/modules/lua_bridge/api/lua_ipc.h>

#include <glob.h>

#if defined(SHUTTLESOCK_DEBUG_SANITIZE) || defined(SHUTTLESOCK_DEBUG_VALGRIND) || defined(__clang_analyzer__)
#define INIT_LUA_ALLOCS 1
#endif

static int luaS_libloader(lua_State *L) {
  lua_pushvalue(L, lua_upvalueindex(1));
  return 1;
}

bool luaS_register_lib(lua_State *L, const char *name, luaL_Reg *reg) {
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "preload");
  lua_getfield(L, -1, name);
  
  if(!lua_isnil(L, -1)) {
    return luaL_error(L, "can't register C function library \"%s\": package \"%s\" is already in package.preload", name, name);
  }
  lua_pop(L, 1);
  
  int n = 0;
  while(reg[n].name != NULL) n++;
  lua_createtable (L, 0, n);
  luaL_setfuncs(L, reg, 0);
  lua_pushcclosure(L, luaS_libloader, 1);
  lua_setfield(L, -2, name);
  lua_pop(L, 2);
  
  luaL_getsubtable(L, LUA_REGISTRYINDEX, "shuttlesock.registered_libs");
  lua_pushboolean(L, 1);
  lua_setfield(L, -2, name);
  lua_pop(L, 1);
  
  return true;
}

static bool function_result_ok(lua_State *L, bool preserve_result) {
  if(lua_isnil(L, -2)) {
    shuso_t *S = shuso_state(L);
    if(!lua_isstring(L, -1)) {
      shuso_set_error(S, "lua function returned nil with no error message");
      raise(SIGABRT);
    }
    else {
      const char *errstr = lua_tostring(L, -1);
      shuso_set_error(S, "%s", errstr);
    }
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


//fails if there's an error or the function returned nil
//success on anything else, even if the function returns nothing
bool luaS_call_noerror(lua_State *L, int nargs, int nrets) {
  int fn_index = lua_gettop(L) - nargs;
  int stacksize_before = fn_index - 1;
  
  lua_pushcfunction(L, luaS_traceback_error_handler);
  lua_insert(L, fn_index);
  int rc = lua_pcall(L, nargs, nrets, fn_index);
  lua_remove(L, fn_index);
  if (rc != LUA_OK) {  
    return false;
  }
  
  int returned_count = lua_gettop(L) - stacksize_before;
  if(returned_count == 0) {
    //returned nothing
    return true;
  }
  if(lua_toboolean(L, stacksize_before + 1)) {
    return true;
  }
  if(returned_count > 2) {
    lua_pop(L, returned_count - 2);
  }
  lua_remove(L, -2);
  //just the error is left on the stack
  return false;;
}

int luaS_traceback_error_handler(lua_State *L) {
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

int luaS_passthru_error_handler(lua_State *L) {
  printf("ERRO: %s\n", lua_tostring(L, 1));
  if(lua_gettop(L) == 0) {
    lua_pushnil(L);
  }
  return 1;
}

bool luaS_pcall(lua_State *L, int nargs, int nresults) {
#ifndef NDEBUG
  if(!lua_isfunction(L, -(nargs+1))) {
    shuso_log_error(shuso_state(L), "nargs: %i", nargs);
    assert(lua_isfunction(L, -(nargs+1)));
  }
#endif  
  lua_pushcfunction(L, luaS_traceback_error_handler);
  lua_insert(L, 1);
  
  int rc = lua_pcall(L, nargs, nresults, 1);
  if (rc != LUA_OK) {
    shuso_set_error(shuso_state(L), "Lua error: %s", lua_tostring(L, -1));
    lua_pop(L, 1);
    //lua_gc(L, LUA_GCCOLLECT, 0);
  }
  lua_remove(L, 1);
  return rc == LUA_OK;
}

void luaS_call(lua_State *L, int nargs, int nresults) {
#ifdef SHUTTLESOCK_DEBUG_CRASH_ON_LUA_ERROR
  luaS_pcall(L, nargs, nresults);
#else
  if(!luaS_pcall(L, nargs, nresults)) {
    raise(SIGABRT);
  }
#endif
}

char *luaS_dbgval(lua_State *L, int n) {
  static char buf[512];
  int         type = lua_type(L, n);
  const char *typename = lua_typename(L, type);
  const char *str;
  lua_Number  num;
  int         integer;
  
  char *cur = buf;
  
  switch(type) {
    case LUA_TNUMBER:
      if(lua_isinteger(L, n)) {
        integer = lua_tointeger(L, n);
        sprintf(cur, "%s: %d", typename, integer);
      }
      else {
        num = lua_tonumber(L, n);
        sprintf(cur, "%s: %f", typename, num);
      }
      break;
    case LUA_TBOOLEAN:
      sprintf(cur, "%s: %s", typename, lua_toboolean(L, n) ? "true" : "false");
      break;
    case LUA_TSTRING:
      str = lua_tostring(L, n);
      sprintf(cur, "%s: \"%.50s%s\"", typename, str, strlen(str) > 50 ? "..." : "");
      break;
    case LUA_TTABLE:
      lua_getglobal(L, "tostring");
      lua_pushvalue(L, n);
      lua_call(L, 1, 1);
      str = lua_tostring(L, -1);
      cur += sprintf(cur, "%s", str);
      lua_pop(L, 1);
      
      //is it a global?
      lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
      if(lua_compare(L, -1, n, LUA_OPEQ)) {
        //it's the globals table
        sprintf(cur, "%s", " _G");
        lua_pop(L, 1);
        break;
      }
      lua_pushnil(L);
      while(lua_next(L, -2)) {
        if(lua_compare(L, -1, n, LUA_OPEQ)) {
          cur += sprintf(cur, " _G[\"%s\"]", lua_tostring(L, -2));
          lua_pop(L, 2);
          break;
        }
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
      
      //is it a loaded module?
      lua_getglobal(L, "package");
      lua_getfield(L, -1, "loaded");
      lua_remove(L, -2);
      lua_pushnil(L);  // first key
      while(lua_next(L, -2) != 0) {
        
        if(lua_compare(L, -1, n, LUA_OPEQ)) {
        //it's the globals table
          sprintf(cur, " module \"%s\"", lua_tostring(L, -2));
          lua_pop(L, 2);
          break;
        }
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
      break;
    case LUA_TLIGHTUSERDATA:
      lua_getglobal(L, "tostring");
      lua_pushvalue(L, n);
      lua_call(L, 1, 1);
      sprintf(cur, "light %s", lua_tostring(L, -1));
      lua_pop(L, 1);
      break;
    case LUA_TFUNCTION: {
      lua_Debug dbg;
      lua_pushvalue(L, n);
      lua_getinfo(L, ">nSlu", &dbg);
      
      lua_getglobal(L, "tostring");
      lua_pushvalue(L, n);
      lua_call(L, 1, 1);
      
      sprintf(cur, "%s%s%s%s%s%s %s:%d", lua_iscfunction(L, n) ? "c " : "", lua_tostring(L, -1), strlen(dbg.namewhat)>0 ? " ":"", dbg.namewhat, dbg.name?" ":"", dbg.name?dbg.name:"", dbg.short_src, dbg.linedefined);
      lua_pop(L, 1);
      
      break;
    }
    default:
      lua_getglobal(L, "tostring");
      lua_pushvalue(L, n);
      lua_call(L, 1, 1);
      str = lua_tostring(L, -1);
      sprintf(cur, "%s", str);
      lua_pop(L, 1);
  }
  return buf;
}
void luaS_printstack_named(lua_State *L, const char *name) {
  int        top = lua_gettop(L);
  shuso_t   *S = shuso_state(L);
  shuso_log_warning(S, "lua stack %s:", name);
  for(int n=top; n>0; n--) {
    shuso_log_warning(S, "  [%-2i  %i]: %s", -(top-n+1), n, luaS_dbgval(L, n));
  }
}

void luaS_mm(lua_State *L, int stack_index) {
  assert(lua_gettop(L) >= abs(stack_index));
  int absindex = lua_absindex(L, stack_index);
  lua_getglobal(L, "require");
  lua_pushliteral(L, "mm");
  lua_call(L, 1, 1);
  lua_pushvalue(L, absindex);
  lua_call(L, 1, 0);
}

void luaS_push_inspect_string(lua_State *L, int stack_index) {
  assert(lua_gettop(L) >= abs(stack_index));
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

int luaS_table_concat(lua_State *L, const char *delimeter) {
  luaL_checktype(L, -1, LUA_TTABLE);
  lua_getglobal(L, "table");
  lua_getfield(L, -1, "concat");
  lua_remove(L, -2);
  lua_pushvalue(L, -2);
  lua_pushliteral(L, " ");
  luaS_call(L, 2, 1);
  lua_remove(L, -2);
  return 1;
}

void luaS_do_embedded_script(lua_State *L, const char *name, int nargs) {
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
        return;
      }
      if(nargs > 0) {
        lua_insert(L, -nargs-1);
      }
      lua_call(L, nargs, 1);
      return;
    }
  }
  luaL_error(L, "embedded script %s not found", name);
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

static int luaS_require_embedded_script(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  luaS_do_embedded_script(L, name, 0);
  return 1;
}

bool shuso_lua_initialize(shuso_t *S) {
  lua_State *L = S->lua.state;
  luaS_set_shuttlesock_state_pointer(L, S);
  
  lua_pushstring(L, SHUTTLESOCK_VERSION_STRING);
  lua_setglobal(L, "_SHUTTLESOCK_VERSION");
  
#ifdef SHUTTLESOCK_DEBUG_LUACOV
  lua_getglobal(L, "require");
  lua_pushliteral(L, "luacov");
  lua_call(L, 1, 1);
  //luacov can't be writing to the same outfile from multiple Lua states
  lua_getfield(L, -1, "configuration");
  lua_getfield(L, -1, "statsfile");
  lua_setfield(L, -2, "original_statsfile");
  lua_getfield(L, -1, "statsfile");
  
  struct timeval tv;
  gettimeofday(&tv,NULL);
  lua_pushfstring(L, "%s.split.%d.%d.%d.%p", lua_tostring(L, -1), tv.tv_sec, tv.tv_usec, (long )getpid(), (void *)S);
  lua_setfield(L, -3, "statsfile");
  lua_pop(L, 3);
#endif
  
  luaL_requiref(L, "shuttlesock.core", luaS_push_core_module, 0);
  lua_pop(L, 1);
  
  luaL_requiref(L, "shuttlesock.system", luaS_push_system_module, 0);
  lua_pop(L, 1);
  
  luaL_requiref(L, "shuttlesock.core.lazy_atomics", luaS_push_lazy_atomics_module, 0);
  lua_pop(L, 1);
  
  for(shuso_lua_embedded_scripts_t *script = &shuttlesock_lua_embedded_scripts[0]; script->name != NULL; script++) {
    if(script->module) {
      lua_getglobal(L, "package");
      lua_getfield(L, -1, "preload");
      lua_pushcfunction(L, luaS_require_embedded_script);
      lua_setfield(L, -2, script->name);
      lua_pop(L, 2);
    }
  }
  
  if(S->procnum < SHUTTLESOCK_MANAGER) {
    shuso_register_lua_ipc_handler(S);
  }
  
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

bool luaS_push_lua_module_field(lua_State *L, const char *module_name, const char *key_name) {
  lua_getglobal(L, "require");
  lua_pushstring(L, module_name);
  lua_call(L, 1, 1);
  lua_getfield(L, -1, key_name);
  lua_remove(L, -2);
  return true;
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
    lua_State       *state;
    lua_reference_t  copies_ref;
    lua_reference_t  modules_ref;
  }          src;
  struct {
    lua_State       *state;
    lua_reference_t  copies_ref;
  }          dst;
} lua_gxcopy_state_t;


static bool gxcopy_any(lua_gxcopy_state_t *gxs);
static bool gxcopy_function(lua_gxcopy_state_t *gxs, bool copy_upvalues);

static bool gxcopy_package_loaded(lua_gxcopy_state_t *gxs) {
  lua_State *Ls = gxs->src.state, *Ld = gxs->dst.state;
  luaL_checkstack(Ls, 2, NULL);
  
  lua_rawgeti(Ls, LUA_REGISTRYINDEX, gxs->src.modules_ref);
  lua_pushvalue(Ls, -2);
  lua_gettable(Ls, -2);
  
  if(lua_isnil(Ls, -1)) {
    lua_pop(Ls, 2);
    return false;
  }
  else if(!lua_isstring(Ls, -1)) {
    raise(SIGABRT); //how?..
  }
  
  lua_remove(Ls, -2);
  
  luaL_checkstack(Ld, 3, NULL);
  lua_getglobal(Ld, "require");
  lua_pushstring(Ld, lua_tostring(Ls, -1));
  lua_pop(Ls, 1);
  lua_call(Ld, 1, 1);
  
  assert(lua_type(Ls, -1) == lua_type(Ld, -1));
  return true;
}

static bool gxcopy_cache_load(lua_gxcopy_state_t *gxs) {
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

static bool gxcopy_cache_store(lua_gxcopy_state_t *gxs) {
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

static bool gxcopy_metatable(lua_gxcopy_state_t *gxs) {
  lua_State *Ls = gxs->src.state, *Ld = gxs->dst.state;
  luaL_checkstack(Ls, 3, NULL);
  luaL_checkstack(Ld, 3, NULL);
  assert(lua_istable(Ls, -1));
  if(!lua_getmetatable(Ls, -1)) {
    return true;
  }
  
  if(gxcopy_package_loaded(gxs)) {
    lua_pop(Ls, 1);
    lua_setmetatable(Ld, -2);
    return true;
  }
  
  if(lua_getfield(Ls, -1, "__gxcopy_metatable") == LUA_TFUNCTION) {
    if(!gxcopy_function(gxs, false)) {
      return shuso_set_error(shuso_state(Ls), "failed to gxcopy metatable __gxcopy_metatable function");
    }
    lua_pop(Ls, 2);
    luaS_call(Ld, 0, 1);
    if(lua_isnil(Ld, -1)) {
      return shuso_set_error(shuso_state(Ls), "failed to gxcopy metatable, __gxcopy_metatable function returned nil");
    }
    
    lua_setmetatable(Ld, -2);
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

static bool gxcopy_table(lua_gxcopy_state_t *gxs) {
  lua_State *Ls = gxs->src.state, *Ld = gxs->dst.state;
  
  lua_rawgeti(Ls, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
  if(lua_compare(Ls, -1, -2, LUA_OPEQ)) {
    //it's the globals table
    lua_pop(Ls, 1);
    lua_rawgeti(Ld, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    return true;
  }
  lua_pop(Ls, 1);
  
  if(gxcopy_package_loaded(gxs)) {
    return true;
  }
  
  if(gxcopy_cache_load(gxs)) {
    return true;
  }
  
  luaL_checkstack(Ld, 1, NULL);
  int tindex = lua_absindex(Ls, -1);
  
  lua_newtable(Ld);
  gxcopy_cache_store(gxs);
  
  luaL_checkstack(Ls, 4, NULL);
  
  lua_pushnil(Ls);  /* first key */
  while(lua_next(Ls, tindex) != 0) {
    //key at -2, value at -1
    lua_pushvalue(Ls, -2);
    if(!gxcopy_any(gxs)) { //copy key
      return false;
    }
    lua_pop(Ls, 1);
    if(!gxcopy_any(gxs)) { //copy value
      return false;
    }
    lua_pop(Ls, 1);
    lua_rawset(Ld, -3);
  }
  if(!gxcopy_metatable(gxs)) {
    return false;
  }
  return true;
}

static bool gxcopy_upvalues(lua_gxcopy_state_t *gxs, int nups) {
  lua_State *Ls = gxs->src.state;
  luaL_checkstack(Ls, 1, NULL);
  for(int i=1; i<=nups; i++) {
    if(lua_getupvalue(Ls, -1, i) == NULL) {
      return false;
    }
    if(!gxcopy_any(gxs)) {
      return false;
    }
    lua_pop(Ls, 1);
  }
  return true;
}

static bool gxcopy_function(lua_gxcopy_state_t *gxs, bool copy_upvalues) {
  lua_State *Ls = gxs->src.state, *Ld = gxs->dst.state;
  
  if(gxcopy_package_loaded(gxs)) {
    return true;
  }
  
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
    if(copy_upvalues && dbg.nups > 0) {
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
  gxcopy_cache_store(gxs);
  
  int funcidx = lua_absindex(Ld, -1);
  if(copy_upvalues && dbg.nups > 0) {
    if(!gxcopy_upvalues(gxs, dbg.nups)) {
      return false; 
    }
    for(int i=dbg.nups; i>0; i--) {
      if(!lua_setupvalue(Ld, funcidx, i)) {
        return shuso_set_error(shuso_state(Ls), "failed to gxcopy function %s, setupvalue %i failed", dbg.name, i);
      }
    }
  }
  
  return true;
}

static bool gxcopy_userdata(lua_gxcopy_state_t *gxs) {
  lua_State *Ls = gxs->src.state, *Ld = gxs->dst.state;
  if(gxcopy_package_loaded(gxs)) {
    return true;
  }
  
  if(!lua_getmetatable(Ls, -1)) {
    return shuso_set_error(shuso_state(gxs->src.state), "failed to gxcopy userdata without metatable: this is impossible");
  }
  lua_getfield(Ls, -1, "__gxcopy_save");
  if(!lua_isfunction(Ls, -1)) {
    lua_pop(Ls, 2);
    return shuso_set_error(shuso_state(gxs->src.state), "failed to gxcopy userdata: __gxcopy_save metatable value is not a function");
  }
  
  lua_getfield(Ls, -2, "__gxcopy_load");
  if(!lua_isfunction(Ls, -1)) {
    return shuso_set_error(shuso_state(gxs->src.state), "failed to gxcopy userdata: __gxcopy_load metatable value is not a function");
  }
  if(!gxcopy_any(gxs)) { //copy __gxcopy_load
    lua_pop(Ls, 3);
    return false;
  }
  lua_pop(Ls, 1);
  
  lua_remove(Ls, -2); //remove metatable
  
  lua_pushvalue(Ls, -2);
  if(!luaS_function_call_result_ok(Ls, 1, true)) {
    lua_pop(Ld, 1);
    return shuso_set_error(shuso_state(Ls), "failed to gxcopy userdata: __gxcopy_save error: %s", shuso_last_error(shuso_state(Ls)));
  }
  
  gxcopy_any(gxs); //copy __gxcopy_save'd userdata
  if(!luaS_function_call_result_ok(Ld, 1, true)) {
    return shuso_set_error(shuso_state(Ld), "failed to gxcopy userdata: __gxcopy_load error: %s", shuso_last_error(shuso_state(Ld)));
  }
  lua_pop(Ls, 1); //pop __gxcopy_save'd result
  return true;
}
static bool gxcopy_thread(lua_gxcopy_state_t *gxs) {
  return shuso_set_error(shuso_state(gxs->src.state), "failed to gxcopy coroutine: this is impossible");
}


static bool gxcopy_any(lua_gxcopy_state_t *gxs) {
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
      if(!gxcopy_function(gxs, true)) {
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

bool luaS_gxcopy_start(lua_State *Ls, lua_State *Ld) {
  lua_getfield(Ls, LUA_REGISTRYINDEX, "___gxcopy_state___");
  if(!lua_isnil(Ls, -1)) {
    lua_pop(Ls, 1);
    return shuso_set_error(shuso_state(Ls), "tried to start gxcopy, but another gxcopy is already in progress");
  }
  lua_pop(Ls, 1);
  
  lua_getfield(Ld, LUA_REGISTRYINDEX, "___gxcopy_state___");
  if(!lua_isnil(Ld, -1)) {
    lua_pop(Ld, 1);
    return shuso_set_error(shuso_state(Ls), "tried to start gxcopy, but another gxcopy is already in progress");
  }
  lua_pop(Ld, 1);
  
  lua_gxcopy_state_t *gxs = lua_newuserdata(Ls, sizeof(*gxs));
  lua_setfield(Ls, LUA_REGISTRYINDEX, "___gxcopy_state___");
  
  lua_pushlightuserdata(Ld, gxs);
  lua_setfield(Ld, LUA_REGISTRYINDEX, "___gxcopy_state___");
  
  lua_newtable(Ls);
  int src_copies_ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
  
  lua_newtable(Ld);
  int dst_copies_ref = luaL_ref(Ld, LUA_REGISTRYINDEX);
  
  *gxs = (lua_gxcopy_state_t ){
    .src.state = Ls,
    .src.copies_ref = src_copies_ref,
    .src.modules_ref = LUA_REFNIL,
    .dst.state = Ld,
    .dst.copies_ref = dst_copies_ref,
  };
  
    
  lua_newtable(Ls);
  //invert the package.loaded table
  lua_getglobal(Ls, "package");
  lua_getfield(Ls, -1, "loaded");
  lua_remove(Ls, -2);
  lua_pushnil(Ls);  /* first key */
  while(lua_next(Ls, -2) != 0) {
    lua_pushvalue(Ls, -2);
    lua_settable(Ls, -5);
  }
  lua_pop(Ls, 1);
  int src_modules_ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
  gxs->src.modules_ref = src_modules_ref;
  return true;
}
bool luaS_gxcopy(lua_State *Ls, lua_State *Ld) {
  lua_getfield(Ls, LUA_REGISTRYINDEX, "___gxcopy_state___");
  if(!lua_isuserdata(Ls, -1)) {
    lua_pop(Ls, 1);
    return shuso_set_error(shuso_state(Ls), "tried to gxcopy a Lua value, but no gxcopy is in progress");
  }
  lua_gxcopy_state_t *gxs = (void *)lua_topointer(Ls, -1);
  lua_pop(Ls, 1);
  if(gxs->src.state == Ld && gxs->dst.state == Ls) {
    return shuso_set_error(shuso_state(Ls), "tried to gxcopy a Lua value backwards from destination to source");
  }
  assert(gxs->src.state == Ls && gxs->dst.state == Ld);
  return gxcopy_any(gxs);
}
bool luaS_gxcopy_finish(lua_State *Ls, lua_State *Ld) {
  lua_getfield(Ls, LUA_REGISTRYINDEX, "___gxcopy_state___");
  if(!lua_isuserdata(Ls, -1)) {
    lua_pop(Ls, 1);
    return shuso_set_error(shuso_state(Ls), "tried to finish gxcopy, but no gxcopy is in progress");
  }
  lua_gxcopy_state_t *gxs = (void *)lua_topointer(Ls, -1);
  lua_pop(Ls, 1);
  assert(gxs->src.state == Ls && gxs->dst.state == Ld);
  luaL_unref(Ls, LUA_REGISTRYINDEX, gxs->src.copies_ref);
  luaL_unref(Ls, LUA_REGISTRYINDEX, gxs->src.modules_ref);
  luaL_unref(Ld, LUA_REGISTRYINDEX, gxs->dst.copies_ref);
  
  lua_pushnil(Ls);
  lua_setfield(Ls, LUA_REGISTRYINDEX, "___gxcopy_state___");
  
  lua_pushnil(Ld);
  lua_setfield(Ld, LUA_REGISTRYINDEX, "___gxcopy_state___");
  return true;
}

bool luaS_gxcopy_module_state(lua_State *Ls, lua_State *Ld, const char *module_name) {
  
  lua_getglobal(Ls, "require");
  lua_pushstring(Ls, module_name);
  if(!luaS_function_pcall_result_ok(Ls, 1, true)) {
    return shuso_set_error(shuso_state(Ls), "gxcopy_module_state error: no Lua module '%s' in source Lua state", module_name);
  }
  
  lua_getglobal(Ld, "require");
  lua_pushstring(Ld, module_name);
  if(!luaS_function_pcall_result_ok(Ld, 1, true)) {
    return shuso_set_error(shuso_state(Ls), "gxcopy_module_state error: no Lua module '%s' in destination Lua state", module_name);
  }
  
  if(!lua_getmetatable(Ls, -1)) {
    lua_pop(Ls, 2);
    return shuso_set_error(shuso_state(Ls), "gxcopy_module_state error: Lua module '%s' has no metatable in source Lua state", module_name);
  }
  
  if(!lua_getmetatable(Ld, -1)) {
    lua_pop(Ld, 2);
    return shuso_set_error(shuso_state(Ls), "gxcopy_module_state error: Lua module '%s' has no metatable in destination Lua state", module_name);
  }
  
  lua_getfield(Ls, -1, "__gxcopy_save_module_state");
  if(!lua_isfunction(Ls, -1)) {
    lua_pop(Ls, 3);
    return shuso_set_error(shuso_state(Ls), "gxcopy_module_state error: Lua module '%s' has no __gxcopy_save_module_state metatable field in destination Lua state", module_name);
  }
  
  lua_getfield(Ld, -1, "__gxcopy_load_module_state");
  if(!lua_isfunction(Ld, -1)) {
    lua_pop(Ld, 3);
    return shuso_set_error(shuso_state(Ls), "gxcopy_module_state error: Lua module '%s' has no __gxcopy_load_module_state metatable field in destination Lua state", module_name);
  }
  
  //__gxcopy_save_module_state() -> table
  if(!luaS_function_pcall_result_ok(Ls, 0, true)) {
    return shuso_set_error(shuso_state(Ls), "gxcopy_module_state error: %s", module_name, shuso_last_error(shuso_state(Ls)));
    return false;
  }
  
  luaS_gxcopy(Ls, Ld);
  lua_pop(Ls, 3);
  if(!luaS_pcall(Ld, 1, 0)) {
    return shuso_set_error(shuso_state(Ls), "gxcopy_module_state error: Lua module '%s' __gxcopy_load_module_state failed: %s", module_name, shuso_last_error(shuso_state(Ld)));
  }
  return true;
}
bool luaS_streq(lua_State *L, int index, const char *str) {
  if(str) {
    index = lua_absindex(L, index);
    lua_pushstring(L, str);
  }
  bool equal = lua_compare(L, index, -1, LUA_OPEQ);
  lua_pop(L, 1);
  return equal;
}
int luaS_table_count(lua_State *L, int idx) {
  int absidx = lua_absindex(L, idx);
  assert(lua_type(L, absidx) == LUA_TTABLE);
  lua_pushnil(L);
  int count = 0;
  while(lua_next(L, absidx) != 0) {
    //key at -2, value at -1
    lua_pop(L, 1);
    count++;
  }
  return count;
}

bool luaS_gxcopy_package_preloaders(lua_State *Ls, lua_State *Ld) {
  bool ok = true;
  
  luaL_checkstack(Ls, 4, NULL);
  luaL_checkstack(Ld, 3, NULL);
  
  lua_getglobal(Ls, "package");
  lua_getfield(Ls, -1, "preload");
  lua_remove(Ls, -2);
  int Ls_preload = lua_gettop(Ls);
  
  lua_getglobal(Ld, "package");
  lua_getfield(Ld, -1, "preload");
  lua_remove(Ld, -2);
  int Ld_preload = lua_gettop(Ld);
  
  lua_pushnil(Ls);
  while(lua_next(Ls, -2)) {
    lua_pop(Ls, 1);
    const char *k = lua_tostring(Ls, -1);
    lua_getfield(Ls, Ls_preload, k);
    if(luaS_gxcopy(Ls, Ld)) {
      lua_setfield(Ld, Ld_preload, k);
    }
    else {
      shuso_set_error(shuso_state(Ls), "Failed to gxcopy Lua package.preload[\"%s\"]", k);
      ok = false;
    }
    lua_pop(Ls, 1);
  }
  lua_pop(Ls, 1);
  lua_pop(Ld, 1);
  return ok;
}

bool luaS_pointer_ref(lua_State *L, const char *pointer_table_name, const void *ptr) {
  lua_getfield(L, LUA_REGISTRYINDEX, pointer_table_name);
  if(lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, pointer_table_name);
  }
  lua_pushlightuserdata(L, (void *)ptr);
  lua_pushvalue(L, -3);
  lua_settable(L, -3);
  lua_pop(L, 2);
  return true;
}

bool luaS_pointer_unref(lua_State *L, const char *pointer_table_name, const void *ptr) {
  lua_getfield(L, LUA_REGISTRYINDEX, pointer_table_name);
  if(!lua_isnil(L, -1)) {
    lua_pushlightuserdata(L, (void *)ptr);
    lua_pushnil(L);
    lua_settable(L, -3);
  }
  lua_pop(L, 1);
  return true;
}

bool luaS_get_pointer_ref(lua_State *L, const char *pointer_table_name, const void *ptr) {
  lua_checkstack(L, 2);
  lua_getfield(L, LUA_REGISTRYINDEX, pointer_table_name);
  if(!lua_istable(L, -1)) {
    lua_pop(L, 1);
    lua_pushnil(L);
  }
  else {
    lua_pushlightuserdata(L, (void *)ptr);
    lua_gettable(L, -2);
    lua_remove(L, -2);
  }
  return true;
}

void luaS_push_runstate(lua_State *L, shuso_runstate_t state) {
  switch(state) {
    case SHUSO_STATE_DEAD:
      lua_pushliteral(L, "dead");
      return;
    case SHUSO_STATE_STOPPED:
      lua_pushliteral(L, "stopped");
      return;
    case SHUSO_STATE_MISCONFIGURED:
      lua_pushliteral(L, "misconfigured");
      return;
    case SHUSO_STATE_CONFIGURING:
      lua_pushliteral(L, "configuring");
      return;
    case SHUSO_STATE_CONFIGURED:
      lua_pushliteral(L, "configured");
      return;
    case SHUSO_STATE_NIL:
      lua_pushnil(L);
      return;
    case SHUSO_STATE_STARTING:
      lua_pushliteral(L, "starting");
      return;
    case SHUSO_STATE_RUNNING:
      lua_pushliteral(L, "running");
      return;
    case SHUSO_STATE_STOPPING:
      lua_pushliteral(L, "stopping");
      return;
  }
#ifndef __clang_analyzer__
  shuso_set_error(shuso_state(L), "unknown runstate, can't push onto Lua stack");
  lua_pushnil(L);
  return;
#endif
}
