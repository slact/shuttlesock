
#include <shuttlesock.h>
#include <shuttlesock/embedded_lua_scripts.h>
#include <shuttlesock/modules/lua_bridge/api/lazy_atomics.h>
#include <shuttlesock/modules/lua_bridge/api/ipc_lua_api.h>
#include <sys/types.h>
#include <unistd.h>
#include <glob.h>
#include <arpa/inet.h>

#define SHUTTLESOCK_DEBUG_CRASH_ON_LUA_ERROR 1

#if defined(SHUTTLESOCK_DEBUG_SANITIZE) || defined(SHUTTLESOCK_DEBUG_VALGRIND) || defined(__clang_analyzer__)
#define INIT_LUA_ALLOCS 1 //don't need this since we started building Lua statically when sanitizing
#endif

static int luaS_libloader(lua_State *L) {
  lua_pushvalue(L, lua_upvalueindex(1));
  return 1;
}

bool luaS_register_lib(lua_State *L, const char *name, luaL_Reg *reg) {
  luaL_checkstack(L, 3, NULL);
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "preload");
  lua_getfield(L, -1, name);
  
  if(!lua_isnil(L, -1)) {
    return luaL_error(L, "can't register C function library \"%s\": package \"%s\" is already in package.preload", name, name);
  }
  lua_pop(L, 1);
  
  
  luaL_checkstack(L, 3, NULL);
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

//fails if there's an error or the function returned nil
//success on anything else, even if the function returns nothing
bool luaS_call_noerror(lua_State *L, int nargs, int nrets) {
  int fn_index = lua_gettop(L) - nargs;
  int stacksize_before = fn_index - 1;
  
  luaL_checkstack(L, 1, NULL);
  
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
  luaL_traceback(L, L, lua_tostring(L, -1), 1);
  return 1;
}

int luaS_passthru_error_handler(lua_State *L) {
  //printf("ERRO: %s\n", lua_tostring(L, 1));
  if(lua_gettop(L) == 0) {
    lua_checkstack(L, 1);
    lua_pushnil(L);
  }
  return 1;
}

bool luaS_pcall(lua_State *L, int nargs, int nresults) {
#ifndef NDEBUG
  if(!lua_isfunction(L, -(nargs+1))) {
    luaS_printstack(L, "luaS_pcall on not-a-function");
    shuso_log_error(shuso_state(L), "nargs: %i", nargs);
    assert(lua_isfunction(L, -(nargs+1)));
  }
#endif
  luaL_checkstack(L, nresults+2, NULL);
  lua_pushcfunction(L, luaS_traceback_error_handler);
  lua_insert(L, 1);
  
  int rc = lua_pcall(L, nargs, nresults, 1);
  if (rc != LUA_OK) {
    shuso_t *S = shuso_state(L);
    bool current_nolog = S->error.do_not_log;
    S->error.do_not_log = false;
    shuso_set_error(shuso_state(L), "Lua error: %s", lua_tostring(L, -1));
    S->error.do_not_log = current_nolog;
    lua_pop(L, 1);
    //lua_gc(L, LUA_GCCOLLECT, 0);
  }
  lua_remove(L, 1);
  return rc == LUA_OK;
}

void luaS_call(lua_State *L, int nargs, int nresults) {
#ifndef SHUTTLESOCK_DEBUG_CRASH_ON_LUA_ERROR
  luaS_pcall(L, nargs, nresults);
#else
  if(!luaS_pcall(L, nargs, nresults)) {
    raise(SIGABRT);
  }
#endif
}

char *luaS_dbgval(lua_State *L, int n, char *buf, size_t buflen) {
  int         type = lua_type(L, n);
  const char *typename = lua_typename(L, type);
  const char *str;
  lua_Number  num;
  int         integer;
  
  char *cur = buf;
  const char *last = &buf[buflen-2];

  int top = lua_gettop(L);
  size_t    sz;
  switch(type) {
    case LUA_TNUMBER:
      if(lua_isinteger(L, n)) {
        integer = lua_tointeger(L, n);
        snprintf(cur, last - cur, "%s: %d", typename, integer);
      }
      else {
        num = lua_tonumber(L, n);
        snprintf(cur, last - cur, "%s: %f", typename, num);
      }
      break;
    case LUA_TBOOLEAN:
      snprintf(cur, last - cur, "%s: %s", typename, lua_toboolean(L, n) ? "true" : "false");
      break;
    case LUA_TSTRING:
      str = lua_tolstring(L, n, &sz);
      snprintf(cur, last - cur, "%s: \"%.50s%s\"[%i]", typename, str, strlen(str) > 50 ? "..." : "", (int )sz);
      break;
    case LUA_TTABLE:
      luaL_checkstack(L, 8, NULL);
      if(lua_status(L) == LUA_OK) {
        lua_getglobal(L, "tostring");
        lua_pushvalue(L, n);
        lua_call(L, 1, 1);
        str = lua_tostring(L, -1);
        cur += snprintf(cur, last - cur, "%s", str);
        lua_pop(L, 1);
      }
      else {
        cur += snprintf(cur, last - cur, "table: %p", lua_topointer(L, n));
      }
      
      //is it a global?
      lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
      if(lua_compare(L, -1, n, LUA_OPEQ)) {
        //it's the globals table
        snprintf(cur, last - cur, "%s", " _G");
        lua_pop(L, 1);
        break;
      }
      lua_pushnil(L);
      while(lua_next(L, -2)) {
        if(lua_compare(L, -1, n, LUA_OPEQ)) {
          cur += snprintf(cur, last - cur, " _G[\"%s\"]", lua_tostring(L, -2));
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
          snprintf(cur, last - cur, " module \"%s\"", lua_tostring(L, -2));
          lua_pop(L, 2);
          break;
        }
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
      break;
    case LUA_TLIGHTUSERDATA:
      luaL_checkstack(L, 2, NULL);
      if(lua_status(L) == LUA_OK) {
        lua_getglobal(L, "tostring");
        lua_pushvalue(L, n);
        lua_call(L, 1, 1);
        snprintf(cur, last - cur, "light %s", lua_tostring(L, -1));
        lua_pop(L, 1);
      }
      else {
        snprintf(cur, last - cur, "light userdata: %p", lua_topointer(L, n));
      }
      break;
    case LUA_TFUNCTION: {
      luaL_checkstack(L, 3, NULL);
      lua_Debug dbg;
      lua_pushvalue(L, n);
      lua_getinfo(L, ">nSlu", &dbg);
      
      if(lua_status(L) == LUA_OK) {
        lua_getglobal(L, "tostring");
        lua_pushvalue(L, n);
        lua_call(L, 1, 1);
      }
      else {
        lua_pushfstring(L, "function: %p", lua_topointer(L, n));
      }
      
      snprintf(cur, last - cur, "%s%s%s%s%s%s %s:%d", lua_iscfunction(L, n) ? "c " : "", lua_tostring(L, -1), strlen(dbg.namewhat)>0 ? " ":"", dbg.namewhat, dbg.name?" ":"", dbg.name?dbg.name:"", dbg.short_src, dbg.linedefined);
      lua_pop(L, 1);
      
      break;
    }
    case LUA_TTHREAD: {
      lua_State *coro = lua_tothread(L, n);
      luaL_checkstack(L, 4, NULL);
      luaL_checkstack(coro, 1, NULL);
      
      if(lua_status(L) == LUA_OK) {
        lua_getglobal(L, "tostring");
        lua_pushvalue(L, n);
        lua_call(L, 1, 1);
      }
      else {
        lua_pushfstring(L, "thread: %p", lua_topointer(L, n));
      }
      
      char *status = NULL;
      switch(lua_status(L)) {
        case LUA_OK: {
          lua_Debug ar;
          if (lua_getstack(L, 0, &ar) > 0) {  // does it have frames? 
            status = "normal";
          }
          else if (lua_gettop(L) == 0) {
            status = "dead";
          }
          else {
            status ="suspended";  // initial state 
          }
          break;
        }
        case LUA_YIELD:
          status = "suspended";
          break;
        default:
          status = "dead";
          break;
      }
      lua_pushstring(L, status);
      
      luaL_where(coro, 1);
      if(L == coro) {
        snprintf(cur, last - cur, "%s (self) (%s) @ %s", lua_tostring(L, -3), lua_tostring(L, -2), lua_tostring(coro, -1));
        lua_pop(L, 3);
      }
      else {
        snprintf(cur, last - cur, "%s (%s) @ %s", lua_tostring(L, -2), lua_tostring(L, -1), lua_tostring(coro, -1));
        lua_pop(L, 2);
        lua_pop(coro, 1);
      }
      break;
    }
    
    case LUA_TNIL:
      sprintf(cur, "%s", "nil");
      break;
    
    default:
      if(lua_status(L) == LUA_OK) {
        luaL_checkstack(L, 2, NULL);
        lua_getglobal(L, "tostring");
        lua_pushvalue(L, n);
        lua_call(L, 1, 1);
        str = lua_tostring(L, -1);
        
        snprintf(cur, last - cur, "%s", str);
        lua_pop(L, 1);
      }
      else {
        snprintf(cur, last - cur, "%s: %p", lua_typename(L, type), lua_topointer(L, n));
      }
      break;
  }
  assert(lua_gettop(L)==top);
  return buf;
}
void luaS_printstack_named(lua_State *L, const char *name) {
  int        top = lua_gettop(L);
  shuso_t   *S = shuso_state(L);
  char dbgval[128];
  char line[256];
  luaL_Buffer buf;
  luaL_buffinit(L, &buf);
  
  sprintf(line, "lua stack %s:", name);
  luaL_addstring(&buf, line);
  
  for(int n=top; n>0; n--) {
    snprintf(line, 256, "\n                               [%-2i  %i]: %s", -(top-n+1), n, luaS_dbgval(L, n, dbgval, sizeof(dbgval)));
    luaL_addstring(&buf, line);
  }
  luaL_checkstack(L, 1, NULL);
  luaL_pushresult(&buf);
  const char *stack = lua_tostring(L, -1);
  shuso_log_warning(S, "%s", stack);
  lua_pop(L, 1);
  assert(lua_gettop(L) == top);
}

void luaS_mm(lua_State *L, int stack_index) {
  assert(lua_gettop(L) >= abs(stack_index));
  int absindex = lua_absindex(L, stack_index);
  luaL_checkstack(L, 2, NULL);
  lua_getglobal(L, "require");
  lua_pushliteral(L, "mm");
  lua_call(L, 1, 1);
  lua_pushvalue(L, absindex);
  lua_call(L, 1, 0);
}

void luaS_push_inspect_string(lua_State *L, int stack_index) {
  assert(lua_gettop(L) >= abs(stack_index));
  int absindex = lua_absindex(L, stack_index);
  luaL_checkstack(L, 2, NULL);
  lua_getglobal(L, "require");
  lua_pushliteral(L, "inspect");
  lua_call(L, 1, 1);
  lua_pushvalue(L, absindex);
  lua_call(L, 1, 1);
}

void luaS_inspect(lua_State *L, int stack_index) {
  luaS_push_inspect_string(L, stack_index);
  luaL_checkstack(L, 2, NULL);
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
  luaL_checkstack(L, 3, NULL);
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
  luaL_checkstack(L, 1, NULL);
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
  luaL_checkstack(L, 1, NULL);
  lua_getfield(L, LUA_REGISTRYINDEX, "shuttlesock.userdata");
  assert(lua_islightuserdata(L, -1));
  shuso_t *S = (shuso_t *)lua_topointer(L, -1);
  lua_pop(L, 1);
  return S;
}

bool luaS_set_shuttlesock_state_pointer(lua_State *L, shuso_t *S) {
  luaL_checkstack(L, 1, NULL);
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
  
#ifdef SHUTTLESOCK_LUA_PACKAGE_PATH
  lua_getglobal(L, "package");
  lua_pushstring(L, SHUTTLESOCK_LUA_PACKAGE_PATH);
  lua_setfield(L, -2, "path");
  lua_pop(L, 1);
#endif
#ifdef SHUTTLESOCK_LUA_PACKAGE_CPATH
  lua_getglobal(L, "package");
  lua_pushstring(L, SHUTTLESOCK_LUA_PACKAGE_CPATH);
  lua_setfield(L, -2, "cpath");
  lua_pop(L, 1);
#endif
  
  luaS_set_shuttlesock_state_pointer(L, S);
  
  luaL_checkstack(L, 1, NULL);
  lua_pushstring(L, SHUTTLESOCK_VERSION_STRING);
  lua_setglobal(L, "_SHUTTLESOCK_VERSION");
  
  
  
#ifdef SHUTTLESOCK_DEBUG_LUACOV
  luaL_checkstack(L, 6, NULL);
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
  
  luaL_checkstack(L, 3, NULL);
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

int luaS_resume(lua_State *thread, lua_State *from, int nargs, int *nresults) {
  int          rc;
  const char  *errmsg;
  int nres;
#if LUA_VERSION_NUM >= 504
  rc = lua_resume(thread, from, nargs, &nres);
#else
  rc = lua_resume(thread, from, nargs);
  nres = lua_gettop(thread);
#endif
  if(nresults != NULL) {
    *nresults = nres;
  }

  //shuso_log_debug(shuso_state(thread), "done with coroutine %p from %p (main %p) rc %d", (void *)thread, (void *)from, (void *)(shuso_state(thread))->lua.state, rc);
  switch(rc) {
    case LUA_OK:
    case LUA_YIELD:
      break;
    default:
      luaL_checkstack(thread, 1, NULL);
      errmsg = lua_tostring(thread, -1);
      luaL_traceback(thread, thread, errmsg, 1);
      shuso_set_error(shuso_state(thread), lua_tostring(thread, -1));
      break;
  }
  return rc;
}

static int auxresume (lua_State *L, lua_State *co, int narg) {
  int status, nres;
  if (!lua_checkstack(co, narg)) {
    lua_pushliteral(L, "too many arguments to resume");
    return -1;  /* error flag */
  }
  assert(co != L);
  lua_xmove(L, co, narg);
#ifdef SHUSO_LUA_DEBUG_ERRORS
    status = luaS_resume(co, NULL, narg, &nres);
#else
  #if LUA_VERSION_NUM >= 504
  status = lua_resume(co, NULL, narg, &nres);
  #else
  status = lua_resume(co, NULL, narg);
  nres = lua_gettop(co);
  #endif
#endif
  if (status == LUA_OK || status == LUA_YIELD) {
    if (!lua_checkstack(L, nres + 1)) {
      lua_pop(co, nres);  /* remove results anyway */
      lua_pushliteral(L, "too many results to resume");
      return -1;  /* error flag */
    }
    lua_xmove(co, L, nres);  /* move yielded values */
    return nres;
  }
  else {
    lua_xmove(co, L, 1);  /* move error message */
    return -1;  /* error flag */
  }
}

int luaS_coroutine_resume(lua_State *L, lua_State *coro, int nargs) { //like coroutine.resume, but handles errors and lua_xmoves between threads
  shuso_t *S = shuso_state(L);
  //shuso_log_debug(S, "luaS_coroutine_resume coroutine %p from %p", (void *)coro, (void *)L);
  assert(lua_gettop(L) >= nargs);
  int r = auxresume(L, coro, nargs);
  //shuso_log_debug(S, "finished luaS_coroutine_resume coroutine %p from %p ret: %d", (void *)coro, (void *)L, r);
  if (r < 0) {
    
    luaL_traceback(L, coro, lua_tostring(L, -1), 0);
    lua_remove(L, -2);
    shuso_set_error(S, lua_tostring(L, -1));
    
    lua_pushboolean(L, 0);
    lua_insert(L, -2);
    return 2;  /* return false + error message */
  }
  else {
    lua_pushboolean(L, 1);
    lua_insert(L, -(r + 1));
    return r + 1;  /* return true + 'resume' returns */
  }
}


int luaS_call_or_resume(lua_State *L, int nargs) {
  int         state_or_func_index = -1 - nargs;
  int         type = lua_type(L, state_or_func_index);
  lua_State  *coro;
  switch(type) {
    case LUA_TFUNCTION:
#ifdef SHUSO_LUA_DEBUG_ERRORS
      luaS_call(L, nargs, 0);
#else
      lua_call(L, nargs, 0);
#endif
      return 0;
    case LUA_TTHREAD:
      coro = lua_tothread(L, state_or_func_index);
      luaL_checkstack(coro, nargs, NULL);
      lua_xmove(L, coro, nargs);
      int nresults;
#ifdef SHUSO_LUA_DEBUG_ERRORS
      luaS_resume(coro, L, nargs, &nresults);
#else
      lua_resume(coro, L, nargs, &nresults);
#endif
      return 0;
    default:
      return luaL_error(L, "attempted to call-or-resume something that's not a function or coroutine");
  }
}

void luaS_push_shuso_error(lua_State *L) {
  shuso_t *S = shuso_state(L);
  const char *errmsg = shuso_last_error(S);
  if(errmsg == NULL) {
    errmsg = "(unknown error)";
  }
  lua_pushstring(L, errmsg);
}

bool luaS_push_lua_module_field(lua_State *L, const char *module_name, const char *key_name) {
  luaL_checkstack(L, 2, NULL);
  lua_getglobal(L, "require");
  lua_pushstring(L, module_name);
  lua_call(L, 1, 1);
  lua_getfield(L, -1, key_name);
  lua_remove(L, -2);
  return true;
}

typedef struct {
  bool          initialized;  /* true iff buffer has been initialized */
  luaL_Buffer   buffer;
} buffer_writer_data_t;

static int lua_function_dump_writer (lua_State *L, const void *b, size_t size, void *pd) {
  buffer_writer_data_t *writer = pd;
  if(!writer->initialized) {
    writer->initialized = true;
    luaL_buffinit(L, &writer->buffer);
  }
  luaL_addlstring(&writer->buffer, (const char *)b, size);
  return 0;
}

int luaS_function_dump(lua_State *L) {
  buffer_writer_data_t bw = { .initialized = false };
  
  luaL_checktype(L, -1, LUA_TFUNCTION);
  luaL_checkstack(L, 3, NULL);
  
  if (lua_dump(L, lua_function_dump_writer, &bw, 0) != 0) {
    return luaL_error(L, "unable to dump given function");
  }
  luaL_pushresult(&bw.buffer);
  
  return 1;
}

typedef struct {
  struct {
    lua_State       *state;
    lua_reference_t  copies_ref;
    lua_reference_t  modules_ref;
    lua_reference_t  preloaders_ref;
  }          src;
  struct {
    lua_State       *state;
    lua_reference_t  copies_ref;
  }          dst;
  int                ignore_loaded_package_count;
} lua_gxcopy_state_t;


static bool gxcopy_any(lua_gxcopy_state_t *gxs);
static bool gxcopy_function(lua_gxcopy_state_t *gxs, bool copy_upvalues);

static bool gxcopy_package_preloader(lua_gxcopy_state_t *gxs, const char *name) {
  bool    ok = true;
  lua_State *Ls = gxs->src.state, *Ld = gxs->dst.state;
  luaL_checkstack(Ls, 8, NULL);
  luaL_checkstack(Ld, 3, NULL);
  
  lua_getglobal(Ls, "package");
  lua_getfield(Ls, -1, "preload");
  lua_getfield(Ls, -1, name);
  if(lua_isnil(Ls, -1)) {
    lua_pop(Ls, 3);
    return true;
  }
  
  lua_rawgeti(Ls, LUA_REGISTRYINDEX, gxs->src.preloaders_ref);
  lua_pushboolean(Ls, 1);
  lua_setfield(Ls, -2, name);
  lua_pop(Ls, 1);
  
  lua_getglobal(Ld, "package");
  lua_getfield(Ld, -1, "preload");
  if(gxcopy_any(gxs)) {
    lua_setfield(Ld, -2, name);
  }
  else {
    ok = false;
  }
  lua_pop(Ld, 2);
  lua_pop(Ls, 3);
  
  lua_rawgeti(Ls, LUA_REGISTRYINDEX, gxs->src.preloaders_ref);
  lua_pushnil(Ls);
  lua_setfield(Ls, -2, name);
  lua_pop(Ls, 1);
  
  if(!ok) {
    return shuso_set_error(shuso_state(Ls), "failed to gxcopy package.preload[\"%s\"]", name);
  }
  return ok;
}

static bool gxcopy_package_loaded(lua_gxcopy_state_t *gxs) {
  lua_State *Ls = gxs->src.state, *Ld = gxs->dst.state;
  if(gxs->ignore_loaded_package_count > 0) {
    gxs->ignore_loaded_package_count--;
    return false;
  }
  
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
  
  lua_rawgeti(Ls, LUA_REGISTRYINDEX, gxs->src.preloaders_ref);
  const char *package_name = lua_tostring(Ls, -2);
  
  lua_getfield(Ls, -1, package_name);
  if(!lua_isnil(Ls, -1)) {
    lua_pop(Ls, 3);
    return false;
  }
  lua_pop(Ls, 2);
  
  if(luaL_getmetafield(Ls, -2, "__gxcopy_loaded_package_directly") != LUA_TNIL && lua_toboolean(Ls, -1)) {
    lua_pop(Ls, 1);
    luaL_checkstack(Ls, 1, NULL);
    lua_pushvalue(Ls, -2);
    
    gxs->ignore_loaded_package_count++;
    if(!gxcopy_any(gxs)) {
      return false;
    }
    luaL_checkstack(Ld, 3, NULL);
    lua_getglobal(Ld, "package");
    lua_getfield(Ld, -1, "loaded");
    lua_remove(Ld, -2);
    lua_pushvalue(Ld, -2);
    lua_setfield(Ld, -2, package_name);
    lua_pop(Ld, 1);
    lua_pop(Ls, 2);
  }
  else {
    gxcopy_package_preloader(gxs, package_name);
    
    luaL_checkstack(Ld, 3, NULL);
    lua_getglobal(Ld, "require");
    lua_pushstring(Ld, package_name);
    lua_pop(Ls, 1);
    lua_call(Ld, 1, 1);
    
    assert(lua_type(Ls, -1) == lua_type(Ld, -1));
  }
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
  luaL_checkstack(Ls, 4, NULL);
  luaL_checkstack(Ld, 4, NULL);
  
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
#ifdef SHUSO_MEMORY_SANITIZER_ENABLED
  memset(&dbg, 0, sizeof(dbg));
#endif
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
  size_t sz = 0;
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
  
  luaL_checkstack(Ls, 4, NULL);
  luaL_checkstack(Ld, 1, NULL);
  
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
    lua_pop(Ls, 1);
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
  luaL_checkstack(Ld, 1, NULL);
  int dst_top = lua_gettop(Ld);
  int type = lua_type(Ls, -1);
  switch(type) {
    case LUA_TNIL:
      lua_pushnil(Ld);
      break;
    case LUA_TNUMBER:
      if(lua_isinteger(Ls, -1)) {
        lua_pushinteger(Ld, lua_tointeger(Ls, -1));
      }
      else {
        lua_pushnumber(Ld, lua_tonumber(Ls, -1));
      }
      break;
    case LUA_TBOOLEAN:
      lua_pushboolean(Ld, lua_toboolean(Ls, -1));
      break;
    case LUA_TSTRING: {
      size_t sz = 0;
      const char *str = lua_tolstring(Ls, -1, &sz);
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
      lua_pushlightuserdata(Ld, (void *)lua_topointer(Ls, -1));
      break;
  }
  assert(dst_top + 1 == lua_gettop(Ld));
  return true;
}

bool luaS_gxcopy_start(lua_State *Ls, lua_State *Ld) {
  luaL_checkstack(Ls, 10, NULL);
  luaL_checkstack(Ld, 10, NULL);
  
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
    .src.preloaders_ref = LUA_REFNIL,
    .dst.state = Ld,
    .dst.copies_ref = dst_copies_ref,
    .ignore_loaded_package_count = 0
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
  
  lua_newtable(Ls);
  gxs->src.preloaders_ref = luaL_ref(Ls, LUA_REGISTRYINDEX);
  return true;
}
bool luaS_gxcopy(lua_State *Ls, lua_State *Ld) {
  luaL_checkstack(Ls, 1, NULL);
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
  luaL_unref(Ls, LUA_REGISTRYINDEX, gxs->src.preloaders_ref);
  luaL_unref(Ld, LUA_REGISTRYINDEX, gxs->dst.copies_ref);
  
  lua_pushnil(Ls);
  lua_setfield(Ls, LUA_REGISTRYINDEX, "___gxcopy_state___");
  
  lua_pushnil(Ld);
  lua_setfield(Ld, LUA_REGISTRYINDEX, "___gxcopy_state___");
  return true;
}

bool luaS_gxcopy_module_state(lua_State *Ls, lua_State *Ld, const char *module_name) {
  shuso_t        *Ss = shuso_state(Ls);
  int             s_top = lua_gettop(Ls);
  int             d_top = lua_gettop(Ld);
  luaL_checkstack(Ls, 5, NULL);
  luaL_checkstack(Ld, 5, NULL);
  
  lua_getglobal(Ls, "require");
  lua_pushstring(Ls, module_name);
  if(!luaS_pcall(Ls, 1, 1) || lua_isnil(Ls, -1)) {
    lua_settop(Ls, s_top);
    lua_settop(Ld, d_top);
    return shuso_set_error(Ss, "gxcopy_module_state error: no Lua module '%s' in source Lua state", module_name);
  }
  
  lua_getglobal(Ld, "require");
  lua_pushstring(Ld, module_name);
  if(!luaS_pcall(Ld, 1, 1) || lua_isnil(Ld, -1)) {
    lua_settop(Ls, s_top);
    lua_settop(Ld, d_top);
    return shuso_set_error(Ss, "gxcopy_module_state error: no Lua module '%s' in destination Lua state", module_name);
  }
  
  if(!lua_getmetatable(Ls, -1)) {
    lua_settop(Ls, s_top);
    lua_settop(Ld, d_top);
    return shuso_set_error(Ss, "gxcopy_module_state error: Lua module '%s' has no metatable in source Lua state", module_name);
  }
  
  if(!lua_getmetatable(Ld, -1)) {
    lua_settop(Ls, s_top);
    lua_settop(Ld, d_top);
    return shuso_set_error(Ss, "gxcopy_module_state error: Lua module '%s' has no metatable in destination Lua state", module_name);
  }
  
  lua_getfield(Ls, -1, "__gxcopy_save_module_state");
  if(!lua_isfunction(Ls, -1)) {
    lua_settop(Ls, s_top);
    lua_settop(Ld, d_top);
    return shuso_set_error(Ss, "gxcopy_module_state error: Lua module '%s' has no __gxcopy_save_module_state metatable field in destination Lua state", module_name);
  }
  
  lua_getfield(Ld, -1, "__gxcopy_load_module_state");
  if(!lua_isfunction(Ld, -1)) {
    lua_settop(Ls, s_top);
    lua_settop(Ld, d_top);
    return shuso_set_error(Ss, "gxcopy_module_state error: Lua module '%s' has no __gxcopy_load_module_state metatable field in destination Lua state", module_name);
  }
  
  lua_remove(Ld, -2);
  lua_remove(Ld, -2);
  
  //__gxcopy_save_module_state() -> table
  if(!luaS_pcall(Ls, 0, 2)) {
    lua_settop(Ls, s_top);
    lua_settop(Ld, d_top);
    return shuso_set_error(Ss, "gxcopy_module_state error: Lua module '%s' __gxcopy_save_module_state failed: %s", module_name, shuso_last_error(Ss));
  }
  
  if(lua_isnil(Ls, -2)) {
    const char *err = lua_tostring(Ls, -1);
    if(!err) err = "returned nil with no error, but return must be non-nil";
    shuso_set_error(Ss, "gxcopy_module_state error: Lua module '%s' __gxcopy_save_module_state failed: %s", module_name, err);
    lua_settop(Ls, s_top);
    lua_settop(Ld, d_top);
    return false;
  }
  lua_pop(Ls, 1); //pop 2nd return val
  
  luaS_gxcopy(Ls, Ld);
  lua_pop(Ls, 3);
  if(!luaS_pcall(Ld, 1, 2)) {
    lua_settop(Ls, s_top);
    lua_settop(Ld, d_top);
    return shuso_set_error(Ss, "gxcopy_module_state error: Lua module '%s' __gxcopy_load_module_state failed: %s", module_name, shuso_last_error(shuso_state(Ld)));
  }
  if(lua_isnil(Ld, -2)) {
    const char *err = lua_tostring(Ld, -1);
    if(!err) err = "returned nil with no error, but return must be non-nil";
    shuso_set_error(Ss, "gxcopy_module_state error: Lua module '%s' __gxcopy_load_module_state failed: %s", module_name, err);
    lua_settop(Ls, s_top);
    lua_settop(Ld, d_top);
    return false;
  }
  lua_pop(Ld, 1); //pop 2nd return val
  return true;
}
bool luaS_streq(lua_State *L, int index, const char *str) {
  luaL_checkstack(L, 1, NULL);
  if(str) {
    index = lua_absindex(L, index);
    lua_pushstring(L, str);
  }
  bool equal = lua_compare(L, index, -1, LUA_OPEQ);
  lua_pop(L, 1);
  return equal;
}

bool luaS_streq_any(lua_State *L, int index, int nstrings, ...) {
  const char      *str;
  va_list          ap;
  
  luaL_checkstack(L, 1, NULL);
  index = lua_absindex(L, index);
  
  va_start(ap, nstrings);
  
  for(int i = 0; i < nstrings; i++) {
    str = va_arg(ap, const char *);
    lua_pushstring(L, str);
    if(lua_compare(L, index, -1, LUA_OPEQ)) {
      lua_pop(L, 1);
      va_end(ap);
      return true;
    }
    lua_pop(L, 1);
  }
  va_end(ap);
  return false;
}

int luaS_table_count(lua_State *L, int idx) {
  luaL_checkstack(L, 3, NULL);
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
  luaL_checkstack(L, 5, NULL);
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
  luaL_checkstack(L, 3, NULL);
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
  luaL_checkstack(L, 3, NULL);
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

void luaS_getfield_any(lua_State *L, int table_index, int names_count, ...) {
  va_list       ap;
  const char   *name;
  va_start(ap, names_count);
  for(int i = 0; i < names_count; i++) {
    name = va_arg(ap, const char *);
    lua_getfield(L, table_index, name);
    if(!lua_isnil(L, -1)) {
      va_end(ap);
      return;
    }
    lua_pop(L, 1);
  }
  va_end(ap);
  lua_pushnil(L);  
}

void luaS_push_runstate(lua_State *L, shuso_runstate_t state) {
  luaL_checkstack(L, 1, NULL);
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

int luaS_sockaddr_family_lua_to_c(lua_State *L, int strindex) {
  if(luaS_streq_any(L, strindex, 3, "AF_INET", "IPv4", "ipv4")) {
    return AF_INET;
  }
#ifdef SHUTTLESOCK_HAVE_IPV6
  else if(luaS_streq_any(L, strindex, 3, "AF_INET6", "IPv6", "ipv6")) {
    return AF_INET6;
  }
#endif
  else if(luaS_streq_any(L, strindex, 5, "AF_UNIX", "Unix", "unix", "AF_LOCAL", "local")) {
    return AF_UNIX;
  }
  return AF_UNSPEC;
}

int luaS_sockaddr_lua_to_c(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  shuso_sockaddr_t    *sockaddr = NULL;
  
  if(lua_gettop(L) >= 2) {
    int satype = lua_type(L, 2);
    if(satype != LUA_TLIGHTUSERDATA && satype != LUA_TUSERDATA) {
      lua_pushnil(L);
      lua_pushstring(L, "invalid sockaddr pointer, must be a userdata if present");
      return 2;
    }
    sockaddr = (void *)lua_topointer(L, 2);
    if(!sockaddr) {
      lua_pushnil(L);
      lua_pushstring(L, "NULL sockaddr pointer in light userdata");
      return 2;
    }
  }
  
  luaS_getfield_any(L, 1, 3, "addr_family", "address_family", "family");
  sa_family_t   fam = luaS_sockaddr_family_lua_to_c(L, -1);
  lua_pop(L, 1);
  
  bool          get_addr_and_port = false;
  bool          get_path = false;
  socklen_t     sockaddr_len;
  
  if(fam == AF_INET) {
    sockaddr_len = sizeof(struct sockaddr_in);
    get_addr_and_port = true;
  }
#ifdef SHUTTLESOCK_HAVE_IPV6
  else if(fam == AF_INET6) {
    sockaddr_len = sizeof(struct sockaddr_in6);
    get_addr_and_port = true;
  }
#endif
  else if(fam == AF_UNIX) {
    sockaddr_len =sizeof(struct sockaddr_in6);
    get_path = true;
  }
  else {
    lua_pushnil(L);
    lua_pushfstring(L, "invalid addr_family: %s", lua_isstring(L, -1) ? lua_tostring(L, -1) : "<?>");
    return 2;
  }
  
  if(sockaddr == NULL) {
    sockaddr = lua_newuserdata(L, sockaddr_len);
  }
  
  sockaddr->any.sa_family = fam;
  
  if(get_addr_and_port) {
    const char   *addr_binary = NULL, *addr_str = NULL;
    size_t        addr_binary_sz;
    uint16_t      port;
    lua_getfield(L, 1, "port");
    if(lua_isnil(L, -1)) {
      lua_pushnil(L);
      lua_pushstring(L, "port missing");
      return 2;
    }
    port = lua_tointeger(L, -1);
    lua_pop(L, 1);
#ifdef SHUTTLESOCK_HAVE_IPV6
    if(fam == AF_INET6) {
      sockaddr->in6.sin6_port = htons(port);
    }
#endif
    if(fam == AF_INET) {
      sockaddr->in.sin_port = htons(port);
    }
    
    luaS_getfield_any(L, 1, 2, "addr_binary", "address_binary");
    if(lua_isstring(L, -1)) {
      addr_binary = lua_tolstring(L, -1, &addr_binary_sz);
    }
    lua_pop(L, 1);
    
    luaS_getfield_any(L, 1, 2, "addr", "address");
    if(lua_isstring(L, -1)) {
      addr_str = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    
    if(addr_binary) {
#ifdef SHUTTLESOCK_HAVE_IPV6
      if(fam == AF_INET6) {
        if(addr_binary_sz > sizeof(sockaddr->in6.sin6_addr)) {
          addr_binary_sz = sizeof(sockaddr->in6.sin6_addr);
        }
        memcpy(&sockaddr->in6.sin6_addr, addr_binary, addr_binary_sz);
      }
#endif
      if(fam == AF_INET) {
        if(addr_binary_sz > sizeof(sockaddr->in.sin_addr)) {
          addr_binary_sz = sizeof(sockaddr->in.sin_addr);
        }
        memcpy(&sockaddr->in.sin_addr, addr_binary, addr_binary_sz);
      }
    }
    else if(!addr_binary && !addr_str) {
      lua_pushnil(L);
      lua_pushstring(L, "address and address_binary missing from sockaddr table");
      return 2;
    }
    else if(addr_str) {
      int rc = -1;
#ifdef SHUTTLESOCK_HAVE_IPV6
      if(fam == AF_INET6) {
        rc = inet_pton(fam, addr_str, &sockaddr->in6.sin6_addr);
      }
#endif
      if(fam == AF_INET) {
        rc = inet_pton(fam, addr_str, &sockaddr->in.sin_addr);
      }
      if(rc != 1) {
        lua_pushnil(L);
        return luaL_error(L, "invalid address string");
      }
    }
  }
  if(get_path) {
    assert(fam == AF_UNIX);
    size_t sz;
    lua_getfield(L, 1, "path");
    if(lua_isnil(L, -1)) {
      lua_pushnil(L);
      lua_pushliteral(L, "path missing for Unix address family");
      return 2;
    }
    const char *str = luaL_tolstring(L, -1, &sz);
    sz++; // final null byte
    if(sz > sizeof(sockaddr->un.sun_path)) {
      lua_pushnil(L);
      lua_pushliteral(L, "Unix socket path is too long");
      return 2;
    }
    memcpy(sockaddr->un.sun_path, str, sz);
    lua_pop(L, 1);
  }
  
  return 1;
}

int luaS_sockaddr_c_to_lua(lua_State *L) {
  luaL_checkany(L, 1);
  if(lua_type(L, 1) != LUA_TLIGHTUSERDATA && lua_type(L, 1) != LUA_TUSERDATA) {
    lua_pushnil(L);
    lua_pushstring(L, "First argument isn't a userdata or light userdata");
    return 2;
  }
  
  const shuso_sockaddr_t  *sockaddr = lua_topointer(L, 1);
  if(sockaddr == NULL) {
    lua_pushnil(L);
    lua_pushstring(L, "sockaddr pointer can't be NULL");
    return 2;
  }
  
  int tindex;
  if(lua_gettop(L) >= 2) {
    luaL_checktype(L, 2, LUA_TTABLE);
    tindex = 2;
  }
  else {
    lua_newtable(L);
    tindex = lua_gettop(L);
  }
  
  switch(sockaddr->any.sa_family) {
    case AF_INET: {
      lua_pushliteral(L, "IPv4");
      lua_setfield(L, tindex, "family");
      
      lua_pushinteger(L, ntohs(sockaddr->in.sin_port));
      lua_setfield(L, tindex, "port");
      
      lua_pushlstring(L, (char *)&sockaddr->in.sin_addr.s_addr, sizeof(sockaddr->in.sin_addr.s_addr));
      lua_setfield(L, tindex, "address_binary");
    
      char  address_str[INET_ADDRSTRLEN];
      if(inet_ntop(AF_INET, (char *)&sockaddr->in.sin_addr, address_str, INET_ADDRSTRLEN) == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "Invalid IPv4 binary address");
        return 2;
      }
      lua_pushstring(L, address_str);
      lua_setfield(L, tindex, "address");
      
      break;
    }
    
#ifdef SHUTTLESOCK_HAVE_IPV6
    case AF_INET6: {
      lua_pushliteral(L, "IPv6");
      lua_setfield(L, -2, "family");
      
      lua_pushinteger(L, ntohs(sockaddr->in6.sin6_port));
      lua_setfield(L, tindex, "port");
      
      lua_pushlstring(L, (char *)&sockaddr->in6.sin6_addr.s6_addr, sizeof(sockaddr->in6.sin6_addr.s6_addr));
      lua_setfield(L, -2, "address_binary");
      char address_str[INET6_ADDRSTRLEN];
      if(inet_ntop(AF_INET6, (char *)&sockaddr->in6.sin6_addr, address_str, INET6_ADDRSTRLEN) == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "Invalid IPv6 binary address");
        return 2;
      }
      lua_pushstring(L, address_str);
      lua_setfield(L, -2, "address");
      break;
    }
#endif
    case AF_UNIX:
      lua_pushliteral(L, "Unix");
      lua_setfield(L, -2, "family");
      
      lua_pushstring(L, sockaddr->un.sun_path);
      lua_setfield(L, -2, "path");
      break;
  }
  
  lua_pushvalue(L, tindex);
  return 1;
}

int luaS_string_to_socktype(lua_State *L, int strindex) {
  if(luaS_streq_any(L, strindex, 5, "stream", "STREAM", "SOCK_STREAM", "tcp", "TCP")) {
    return SOCK_STREAM;
  }
  else if(luaS_streq_any(L, strindex, 6, "dgram", "DGRAM", "datagram", "SOCK_DGRAM", "udp", "UDP")) {
    return SOCK_DGRAM;
  }
  else if(luaS_streq_any(L, strindex, 3, "raw", "RAW", "SOCK_RAW")) {
    return SOCK_RAW;
  }
#ifdef SOCK_SEQPACKET
  else if(luaS_streq_any(L, strindex, 2, "seqpacket", "SOCK_SEQPACKET")) {
    return SOCK_SEQPACKET;
  }
#endif
#ifdef SOCK_RDM
  else if(luaS_streq_any(L, strindex, 3, "rdm", "RDM", "SOCK_RDM")) {
    return SOCK_RDM;
  }
#endif
  return 0;
}

void luaS_pushstring_from_socktype(lua_State *L, int socktype) {
  switch(socktype) {
    case SOCK_STREAM:
      lua_pushliteral(L, "stream");
      break;
    case SOCK_DGRAM:
      lua_pushliteral(L, "dgram");
      break;
#ifdef SOCK_SEQPACKET
    case SOCK_SEQPACKET:
      lua_pushliteral(L, "seqpacket");
      break;
#endif
    case SOCK_RAW:
      lua_pushliteral(L, "raw");
      break;
#ifdef SOCK_RDM
    case SOCK_RDM:
      lua_pushliteral(L, "rdm");
      break;
#endif
    default:
      lua_pushnil(L);
      break;
  }
}
