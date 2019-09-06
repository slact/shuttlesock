#include <shuttlesock.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <shuttlesock/embedded_lua_scripts.h>

#include <glob.h>

shuso_t *lua_shuttlesock_context(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "shuttlesock.self");
  shuso_t *ctx = (shuso_t *)lua_topointer(L, -1);
  lua_pop(L, 1);
  return ctx;
}


static char *lua_dbgval(lua_State *L, int n) {
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
  shuso_t   *ctx = lua_shuttlesock_context(L);
  shuso_log_warning(ctx, "lua stack:");
  for(int n=top; n>0; n--) {
    shuso_log_warning(ctx, "  [%i]: %s", n, lua_dbgval(L, n));
  }
}


static int shuso_lua_glob(lua_State *L) {
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

bool shuso_lua_create(shuso_t *ctx) {
  if(ctx->procnum == SHUTTLESOCK_MASTER) {
    assert(ctx->lua.state == NULL);
  }
  if((ctx->lua.state = luaL_newstate()) == NULL) {
    return false;
  }
  luaL_openlibs(ctx->lua.state);
  //initialize shuttlesocky lua env
  
  return true;
}

static int shuso_lua_do_embedded_script(lua_State *L) {
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


bool shuso_lua_initialize(shuso_t *ctx) {
  lua_State *L = ctx->lua.state;
  lua_pushlightuserdata(L, ctx);
  lua_setfield(L, LUA_REGISTRYINDEX, "shuttlesock.self");
  
  for(shuso_lua_embedded_scripts_t *script = &shuttlesock_lua_embedded_scripts[0]; script->name != NULL; script++) {
    if(script->module) {
      luaL_requiref(L, script->name, shuso_lua_do_embedded_script, 0);
      lua_pop(L, 1);
    }
  }
  
  //lua doesn't come with a glob, and config needs it when including files
  lua_getglobal(L, "require");
  lua_pushliteral(L, "shuttlesock.config");
  lua_call(L, 1, 1);
  lua_pushcfunction(L, shuso_lua_glob);
  lua_setfield(L, -2, "glob");
  lua_pop(L, 1);

  ctx->config.index = luaL_ref(L, LUA_REGISTRYINDEX);
  
  return true;
}

bool shuso_lua_destroy(shuso_t *ctx) {
  assert(!ctx->lua.external);
  assert(ctx->lua.state != NULL);
  lua_close(ctx->lua.state);
  ctx->lua.state = NULL;
  return true;
}
