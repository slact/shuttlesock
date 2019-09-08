#include <shuttlesock.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <unistd.h>

shuso_t *shuso_lua_ctx(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "shuttlesock.userdata");
  assert(lua_islightuserdata(L, -1));
  shuso_t *ctx = (shuso_t *)lua_topointer(L, -1);
  lua_pop(L, 1);
  return ctx;
}

bool shuso_lua_set_ctx(shuso_t *ctx) {
  lua_State *L = ctx->lua.state;
  lua_pushlightuserdata(L, ctx);
  lua_setfield(L, LUA_REGISTRYINDEX, "shuttlesock.userdata");
  return true;
}

static void lua_getlib_field(lua_State *L, const char *lib, const char *field) {
  lua_getglobal(L, lib);
  assert(lua_istable(L, -1));
  lua_getfield(L, -1, field);
  lua_remove(L, -2);
}

//create a shuttlesock instance from inside Lua
int Lua_shuso_create(lua_State *L) {
  if(shuso_lua_ctx(L)) {
    luaL_error(L, "shuttlesock instance already exists");
    return 0;
  }
  const char  *err;
  shuso_t     *ctx = shuso_create(&err);
  if(!ctx) {
    lua_pushnil(L);
    lua_pushstring(L, err);
    return 2;
  }
  shuso_lua_set_ctx(ctx);
  lua_pushboolean(L, 1);
  return 1;
}

int Lua_shuso_configure_file(lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  shuso_t    *ctx = shuso_lua_ctx(L);
  if(!shuso_configure_file(ctx, path)) {
    lua_pushnil(L);
    lua_pushstring(L, ctx->errmsg);
    return 2;
  }
  lua_pushboolean(L, 1);
  return 1;
}
int Lua_shuso_configure_string(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
  const char *string = luaL_checkstring(L, 2);
  shuso_t    *ctx = shuso_lua_ctx(L);
  if(!shuso_configure_string(ctx, name, string)) {
    lua_pushnil(L);
    lua_pushstring(L, ctx->errmsg);
    return 2;
  }
  lua_pushboolean(L, 1);
  return 1;
}

typedef struct {
  int ref;
  struct {
    const char   *data;
    size_t        len;
  }             handler_dump[2];
} handlers_data_t;

static void free_handlers_data(lua_State *L, void *pd) {
  handlers_data_t *hd = pd;
  if(hd->ref != LUA_NOREF && hd->ref != LUA_REFNIL) {
    luaL_unref(L, LUA_REGISTRYINDEX, hd->ref);
    hd->ref = LUA_NOREF;
  }
  for(int i=0; i<2; i++) {
    if(hd->handler_dump[i].data) {
      free((void *)hd->handler_dump[i].data);
      hd->handler_dump[i].data = NULL;
    }
  }
  free(hd);
}

static bool lua_run_handler_function(lua_State *L, void *pd, const char *handler_name) {
  handlers_data_t *hd = pd;
  lua_rawgeti(L, LUA_REGISTRYINDEX, hd->ref);
  lua_getfield(L, -1, handler_name);
  if(lua_isnil(L, -1)) {
    return false;
  }
  assert(lua_isfunction(L, -1));
  lua_call(L, 0, 0);
  lua_pop(L, 1);
  return true;
}

static bool lua_run_dumped_handler_function(lua_State *L, void *pd, const char *handler_name, int dump_index) {
  handlers_data_t *hd = pd;
  int rc = luaL_loadbufferx(L, hd->handler_dump[dump_index].data, hd->handler_dump[dump_index].len, handler_name, "b");
  if(rc != LUA_OK) {
    lua_error(L);
  }
  lua_call(L, 0, 0);
  return true;
}

static void start_master_lua_handler(shuso_t *ctx, void *pd) {
  lua_run_handler_function(ctx->lua.state, pd, "start_master");
}
static void stop_master_lua_handler(shuso_t *ctx, void *pd) {
  lua_run_handler_function(ctx->lua.state, pd, "stop_master");
  free_handlers_data(ctx->lua.state, pd);
}
static void start_manager_lua_handler(shuso_t *ctx, void *pd) {
  lua_run_handler_function(ctx->lua.state, pd, "start_manager");
}
static void stop_manager_lua_handler(shuso_t *ctx, void *pd) {
  lua_run_handler_function(ctx->lua.state, pd, "stop_manager");
  free_handlers_data(ctx->lua.state, pd);
}
static void start_worker_lua_handler(shuso_t *ctx, void *pd) {
  lua_run_dumped_handler_function(ctx->lua.state, pd, "start_worker", 0);
}
static void stop_worker_lua_handler(shuso_t *ctx, void *pd) {
  lua_run_dumped_handler_function(ctx->lua.state, pd, "stop_worker", 1);
}


static int lua_function_dump_writer (lua_State *L, const void *b, size_t size, void *B) {
  (void)L;
  luaL_addlstring((luaL_Buffer *) B, (const char *)b, size);
  return 0;
}
static int Lua_function_dump(lua_State *L) {
  luaL_Buffer b;
  int strip = lua_toboolean(L, 2);
  luaL_checktype(L, 1, LUA_TFUNCTION);
  lua_settop(L, 1);
  luaL_buffinit(L,&b);
  if (lua_dump(L, lua_function_dump_writer, &b, strip) != 0)
    return luaL_error(L, "unable to dump given function");
  luaL_pushresult(&b);
  return 1;
}

int Lua_shuso_configure_handlers(lua_State *L) {
  handlers_data_t *hdata = malloc(sizeof(*hdata));
  if(!hdata) {
    luaL_error(L, "unable to allocate handler data");
    return 0;
  }
  
  luaL_checktype(L, 1, LUA_TTABLE);
  
  lua_newtable(L);
  
  struct {
    const char   *name;
    int           dump_index;      
  } handlers[] = {
    {"start_master", -1},
    {"stop_master", -1},
    {"start_manager", -1},
    {"stop_manager", -1},
    {"start_worker", 0}, 
    {"stop_worker", 1},
    {NULL, -1}
  };
  
  for(int i=0; handlers[i].name != NULL; i++) {
    lua_getfield(L, 1, handlers[i].name);
    if(!lua_isfunction(L, -1)) {
      free_handlers_data(L, hdata);
      luaL_error(L, "handler %s must be a function", handlers[i].name);
      return 0;
    }
    if(handlers[i].dump_index < 0) {
      lua_setfield(L, -2, handlers[i].name);
    }
    else {
      Lua_function_dump(L);
      size_t      len;
      const char *str_src = lua_tolstring(L, -1, &len);
      char       *str_dst = malloc(len);
      if(!str_dst) {
        free_handlers_data(L, hdata);
        luaL_error(L, "handler %s could not be dumped", handlers[i].name);
        return 0;
      }
      memcpy((void *)str_dst, str_src, len);
      hdata->handler_dump[handlers[i].dump_index].len = len;
      hdata->handler_dump[handlers[i].dump_index].data = str_dst;
      
      lua_pop(L, 2);
    }
  }
  
  
  hdata->ref = luaL_ref(L, LUA_REGISTRYINDEX);
  shuso_runtime_handlers_t runtime_handlers = {
    .start_master = start_master_lua_handler,
    .stop_master = stop_master_lua_handler,
    .start_manager = start_manager_lua_handler,
    .stop_manager = stop_manager_lua_handler,
    .start_worker = start_worker_lua_handler,
    .stop_worker = stop_worker_lua_handler,
    .privdata = hdata
  };
  shuso_t  *ctx = shuso_lua_ctx(L);
  
  if(!shuso_configure_handlers(ctx, &runtime_handlers)) {
    free_handlers_data(L, hdata);
    luaL_error(L, ctx->errmsg);
    return 0;
  }
  
  lua_pushboolean(L, 1);
  return 1;
}
int Lua_shuso_configure_finish(lua_State *L) {
  shuso_t *ctx = shuso_lua_ctx(L);
  if(!shuso_configure_finish(ctx)) {
    luaL_error(L, ctx->errmsg);
    return 0;
  }
  lua_pushboolean(L, 1);
  return 1;
}

int Lua_shuso_destroy(lua_State *L) {
  shuso_t *ctx = shuso_lua_ctx(L);
  if(!shuso_destroy(ctx)) {
    luaL_error(L, ctx->errmsg);
    return 0;
  }
  lua_pushboolean(L, 1);
  return 1;
}

int Lua_shuso_run(lua_State *L) {
  shuso_t *ctx = shuso_lua_ctx(L);
  if(!shuso_run(ctx)) {
    luaL_error(L, ctx->errmsg);
    return 0;
  }
  lua_pushboolean(L, 1);
  return 1;
}

static shuso_stop_t stop_level_string_arg_to_enum(lua_State *L, const char *default_lvl, int argnum) {
  const char *str = luaL_optlstring(L, argnum, default_lvl, NULL);
  if(strcmp(str, "ask") == 0) {
    return SHUSO_STOP_ASK;
  }
  else if(strcmp(str, "insist") == 0) {
    return SHUSO_STOP_INSIST;
  }
  else if(strcmp(str, "demand") == 0) {
    return SHUSO_STOP_DEMAND;
  }
  else if(strcmp(str, "command") == 0) {
    return SHUSO_STOP_COMMAND;
  }
  else if(strcmp(str, "force") == 0) {
    return SHUSO_STOP_FORCE;
  }
  else {
    luaL_error(L, "stop level must be one of 'ask', 'insist', 'demand', 'command' or 'force'");
    return -1;
  }
}

int Lua_shuso_stop(lua_State *L) {
  shuso_t       *ctx = shuso_lua_ctx(L);
  shuso_stop_t   lvl = stop_level_string_arg_to_enum(L, "ask", 1);
  if(!shuso_stop(ctx, lvl)) {
    luaL_error(L, ctx->errmsg);
    return 0;
  }
  lua_pushboolean(L, 1);
  return 1;
}

int Lua_shuso_spawn_manager(lua_State *L) {
  shuso_t *ctx = shuso_lua_ctx(L);
  if(!shuso_spawn_manager(ctx)) {
    luaL_error(L, ctx->errmsg);
    return 0;
  }
  lua_pushboolean(L, 1);
  return 1;
}
int Lua_shuso_stop_manager(lua_State *L) {
  shuso_t      *ctx = shuso_lua_ctx(L);
  shuso_stop_t  lvl = stop_level_string_arg_to_enum(L, "ask", 1);
  if(!shuso_stop_manager(ctx, lvl)) {
    luaL_error(L, ctx->errmsg);
    return 0;
  }
  lua_pushboolean(L, 1);
  return 1;
}
int Lua_shuso_spawn_worker(lua_State *L) {
  shuso_t    *ctx = shuso_lua_ctx(L);
  int         workernum = ctx->common->process.workers_end;
  shuso_process_t   *proc = &ctx->common->process.worker[workernum];
  if(!shuso_spawn_worker(ctx, proc)) {
    luaL_error(L, ctx->errmsg);
    return 0;
  }
  lua_pushboolean(L, 1);
  return 1;
}
int Lua_shuso_stop_worker(lua_State *L) {
  shuso_t     *ctx = shuso_lua_ctx(L);
  int          workernum = luaL_checkinteger(L, 1);
  if(workernum < ctx->common->process.workers_start || workernum > ctx->common->process.workers_end) {
    luaL_error(L, "invalid worker %d (valid range: %d-%d)", workernum, ctx->common->process.workers_start, ctx->common->process.workers_end);
    return 0;
  }
  shuso_process_t   *proc = &ctx->common->process.worker[workernum];
  shuso_stop_t       lvl = stop_level_string_arg_to_enum(L, "ask", 2);
  if(!shuso_stop_worker(ctx, proc, lvl)) {
    luaL_error(L, ctx->errmsg);
    return 0;
  }
  ctx->common->process.workers_end++;
  lua_pushboolean(L, 1);
  return 1;
}

int Lua_shuso_set_log_fd(lua_State *L) {
  shuso_t       *ctx = shuso_lua_ctx(L);
  luaL_Stream   *io = luaL_checkudata(L, 1, LUA_FILEHANDLE);
  int fd = fileno(io->f);
  if(fd == -1) {
    luaL_error(L, "invalid file");
  }
  int fd2 = dup(fd);
  if(fd2 == -1) {
    luaL_error(L, "couldn't dup file");
  }
  if(!shuso_set_log_fd(ctx, fd2)) {
    luaL_error(L, ctx->errmsg);
    return 0;
  }
  lua_pushboolean(L, 1);
  return 1;
}

int Lua_shuso_set_error(lua_State *L) {
  int            nargs = lua_gettop(L);
  shuso_t       *ctx = shuso_lua_ctx(L);
  luaL_checkstring(L, 1);
  
  lua_getlib_field(L, "string", "format");
  lua_call(L, nargs, 1);
  
  const char    *err = lua_tostring(L, -1);
  shuso_set_error(ctx, err);
  lua_pushboolean(L, 1);
  return 1;
}

//watchers
int Lua_shuso_add_timer_watcher(lua_State *L) {
  return 0;
}
int Lua_shuso_remove_timer_watcher(lua_State *L) {
  return 0;
}

//shared memory slab
int Lua_shuso_shared_slab_alloc_string(lua_State *L) {
  return 0;
}
int Lua_shuso_shared_slab_free_string(lua_State *L) {
  return 0;
}

//resolver
int Lua_shuso_resolve_hostname(lua_State *L) {
  return 0;
}

//logger
int Lua_shuso_log(lua_State *L) {
  return 0;
}

//ipc
int Lua_shuso_ipc_send_fd(lua_State *L) {
  return 0;
}
int Lua_shuso_ipc_receive_fd(lua_State *L) {
  return 0;
}

int Lua_shuso_ipc_open_listener_sockets(lua_State *L) {
  return 0;
}

int Lua_shuso_ipc_add_handler(lua_State *L) {
  return 0;
}
int Lua_shuso_ipc_send(lua_State *L) {
  return 0;
}
int Lua_shuso_ipc_send_workers(lua_State *L) {
  return 0;
}



luaL_Reg shuttlesock_core_module_methods[] = {
// creation
  {"create", Lua_shuso_create},

//configuration
  {"configureFile", Lua_shuso_configure_file},
  {"configureString", Lua_shuso_configure_string},
  {"configureHandlers", Lua_shuso_configure_handlers},
  {"configureFinish", Lua_shuso_configure_finish},
  
  {"destroy", Lua_shuso_destroy},
  
  {"run", Lua_shuso_run},
  {"stop", Lua_shuso_stop},
  
  {"spawnManager", Lua_shuso_spawn_manager},
  {"stopManager", Lua_shuso_stop_manager},
  
  {"spawnWorker", Lua_shuso_spawn_worker},
  {"stopWorker", Lua_shuso_stop_worker},
  
  {"setLogFile", Lua_shuso_set_log_fd},
  
  {"setError", Lua_shuso_set_error},
    
//watchers
  {"addTimer", Lua_shuso_add_timer_watcher},
  {"removeTimer", Lua_shuso_remove_timer_watcher},
  
//shared slab
  {"sharedSlabAllocString", Lua_shuso_shared_slab_alloc_string},
  {"sharedSlabFreeString", Lua_shuso_shared_slab_free_string},

//resolver
  {"resolve", Lua_shuso_resolve_hostname},
  
//logger
  {"log", Lua_shuso_log},

//ipc
  {"sendFile", Lua_shuso_ipc_send_fd},
  {"receiveFile", Lua_shuso_ipc_receive_fd},
  {"openListenerSockets", Lua_shuso_ipc_open_listener_sockets},
  {"addMessageHandler", Lua_shuso_ipc_add_handler},
  {"sendMessage", Lua_shuso_ipc_send},
  {"sendMessageToAllWorkers", Lua_shuso_ipc_send_workers},
  
  {NULL, NULL}
};

int shuso_Lua_shuttlesock_core_module(lua_State *L) {
  luaL_newlib(L, shuttlesock_core_module_methods);
  return 1;
}
