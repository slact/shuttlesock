#include <shuttlesock.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <shuttlesock/embedded_lua_scripts.h>

#include <glob.h>

#if defined(SHUTTLESOCK_SANITIZE) || defined(SHUTTLESOCK_VALGRIND) || defined(__clang_analyzer__)
#define INIT_LUA_ALLOCS 1
#endif


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
    lua_printstack(L);
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

char *lua_dbgval(lua_State *L, int n) {
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
void lua_printstack(lua_State *L) {
  int        top = lua_gettop(L);
  shuso_t   *S = shuso_state(L);
  shuso_log_warning(S, "lua stack:");
  for(int n=top; n>0; n--) {
    shuso_log_warning(S, "  [%i]: %s", n, lua_dbgval(L, n));
  }
}

void lua_mm(lua_State *L, int stack_index) {
  int absindex = lua_absindex(L, stack_index);
  lua_getglobal(L, "require");
  lua_pushliteral(L, "mm");
  lua_call(L, 1, 1);
  lua_pushvalue(L, absindex);
  lua_call(L, 1, 0);
}

static int shuso_Lua_glob(lua_State *L) {
  const char *pattern = luaL_checkstring(L, 1);
  int         rc = 0;
  glob_t      globres;
  rc = glob(pattern, GLOB_ERR | GLOB_MARK, NULL, &globres);
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

int shuso_Lua_do_embedded_script(lua_State *L) {
  const char *name = luaL_checkstring(L, -1);
  shuso_lua_embedded_scripts_t *script;
  for(script = &shuttlesock_lua_embedded_scripts[0]; script->name != NULL; script++) {
    if(strcmp(script->name, name) == 0) {
      int rc = luaL_loadbuffer(L, script->script, script->strlen, script->name);
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


bool shuso_lua_initialize(shuso_t *S) {
  shuso_lua_set_shuttlesock_state_pointer(S);
  lua_State *L = S->lua.state;
  
  luaL_requiref(L, "shuttlesock.core", shuso_Lua_shuttlesock_core_module, 0);
  lua_pop(L, 1);
  
  for(shuso_lua_embedded_scripts_t *script = &shuttlesock_lua_embedded_scripts[0]; script->name != NULL; script++) {
    if(script->module) {
      luaL_requiref(L, script->name, shuso_Lua_do_embedded_script, 0);
      lua_pop(L, 1);
    }
  }
  
  //lua doesn't come with a glob, and config needs it when including files
  lua_getglobal(L, "require");
  lua_pushliteral(L, "shuttlesock.config");
  lua_call(L, 1, 1);
  
  lua_pushcfunction(L, shuso_Lua_glob);
  lua_setfield(L, -2, "glob");
  
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

