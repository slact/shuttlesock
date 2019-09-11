#include <shuttlesock.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <unistd.h>

typedef enum {
  LUA_EV_WATCHER_IO =       0,
  LUA_EV_WATCHER_TIMER =    1,
  LUA_EV_WATCHER_CHILD =    2,
  LUA_EV_WATCHER_SIGNAL =   3
} shuso_lua_ev_watcher_type_t;

typedef struct {
  shuso_ev_any      watcher;
  struct {
    int               self;
    int               handler;
  }                 ref;
  unsigned          type:4;
  lua_State        *coroutine_thread;
} shuso_lua_ev_watcher_t;

static int Lua_watcher_stop(lua_State *L);
static void lua_watcher_unref(lua_State *L, shuso_lua_ev_watcher_t *w);
static const char *watchertype_str(shuso_lua_ev_watcher_type_t type);


typedef struct {
  size_t      len;
  const char  data[];
} shuso_lua_shared_string_t;

shuso_t *shuso_lua_ctx(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "shuttlesock.userdata");
  assert(lua_islightuserdata(L, -1));
  shuso_t *ctx = (shuso_t *)lua_topointer(L, -1);
  lua_pop(L, 1);
  return ctx;
}

static int shuso_lua_resume(lua_State *thread, lua_State *from, int nargs) {
  int          rc;
  const char  *errmsg;
  shuso_t     *ctx;
  rc = lua_resume(thread, from, nargs);
  switch(rc) {
    case LUA_OK:
    case LUA_YIELD:
      break;
    default:
      ctx = shuso_lua_ctx(thread);
      errmsg = lua_tostring(thread, -1);
      luaL_traceback(thread, thread, errmsg, 1);
      shuso_log_error(ctx, "lua coroutine error: %s", lua_tostring(thread, -1));
      lua_pop(thread, 1);
      lua_gc(thread, LUA_GCCOLLECT, 0);
      break;
  }
  return rc;
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
    return luaL_error(L, "shuttlesock instance already exists");
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

static int Lua_shuso_configure_handlers(lua_State *L) {
  shuso_t         *ctx = shuso_lua_ctx(L);
  handlers_data_t *hdata = shuso_stalloc(&ctx->stalloc, sizeof(*hdata));
  if(!hdata) {
    return luaL_error(L, "unable to allocate handler data");
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
      return luaL_error(L, "handler %s must be a function", handlers[i].name);
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
        return luaL_error(L, "handler %s could not be dumped", handlers[i].name);
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
  
  if(!shuso_configure_handlers(ctx, &runtime_handlers)) {
    free_handlers_data(L, hdata);
    return luaL_error(L, ctx->errmsg);
  }
  
  lua_pushboolean(L, 1);
  return 1;
}
static int Lua_shuso_configure_finish(lua_State *L) {
  shuso_t *ctx = shuso_lua_ctx(L);
  if(!shuso_configure_finish(ctx)) {
    return luaL_error(L, ctx->errmsg);
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int Lua_shuso_destroy(lua_State *L) {
  shuso_t *ctx = shuso_lua_ctx(L);
  if(!shuso_destroy(ctx)) {
    return luaL_error(L, ctx->errmsg);
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int Lua_shuso_run(lua_State *L) {
  shuso_t *ctx = shuso_lua_ctx(L);
  if(!shuso_run(ctx)) {
    return luaL_error(L, ctx->errmsg);
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
    return luaL_error(L, "stop level must be one of 'ask', 'insist', 'demand', 'command' or 'force'");
  }
}

static int Lua_shuso_stop(lua_State *L) {
  shuso_t       *ctx = shuso_lua_ctx(L);
  shuso_stop_t   lvl = stop_level_string_arg_to_enum(L, "ask", 1);
  if(!shuso_stop(ctx, lvl)) {
    return luaL_error(L, ctx->errmsg);
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int Lua_shuso_spawn_manager(lua_State *L) {
  shuso_t *ctx = shuso_lua_ctx(L);
  if(!shuso_spawn_manager(ctx)) {
    return luaL_error(L, ctx->errmsg);
  }
  lua_pushboolean(L, 1);
  return 1;
}
static int Lua_shuso_stop_manager(lua_State *L) {
  shuso_t      *ctx = shuso_lua_ctx(L);
  shuso_stop_t  lvl = stop_level_string_arg_to_enum(L, "ask", 1);
  if(!shuso_stop_manager(ctx, lvl)) {
    return luaL_error(L, ctx->errmsg);
  }
  lua_pushboolean(L, 1);
  return 1;
}
static int Lua_shuso_spawn_worker(lua_State *L) {
  shuso_t    *ctx = shuso_lua_ctx(L);
  int         workernum = ctx->common->process.workers_end;
  shuso_process_t   *proc = &ctx->common->process.worker[workernum];
  if(!shuso_spawn_worker(ctx, proc)) {
    return luaL_error(L, ctx->errmsg);
  }
  lua_pushboolean(L, 1);
  return 1;
}
static int Lua_shuso_stop_worker(lua_State *L) {
  shuso_t     *ctx = shuso_lua_ctx(L);
  int          workernum = luaL_checkinteger(L, 1);
  if(workernum < ctx->common->process.workers_start || workernum > ctx->common->process.workers_end) {
    return luaL_error(L, "invalid worker %d (valid range: %d-%d)", workernum, ctx->common->process.workers_start, ctx->common->process.workers_end);
  }
  shuso_process_t   *proc = &ctx->common->process.worker[workernum];
  shuso_stop_t       lvl = stop_level_string_arg_to_enum(L, "ask", 2);
  if(!shuso_stop_worker(ctx, proc, lvl)) {
    return luaL_error(L, ctx->errmsg);
  }
  ctx->common->process.workers_end++;
  lua_pushboolean(L, 1);
  return 1;
}

static int Lua_shuso_set_log_fd(lua_State *L) {
  shuso_t       *ctx = shuso_lua_ctx(L);
  luaL_Stream   *io = luaL_checkudata(L, 1, LUA_FILEHANDLE);
  int fd = fileno(io->f);
  if(fd == -1) {
    return luaL_error(L, "invalid file");
  }
  int fd2 = dup(fd);
  if(fd2 == -1) {
    return luaL_error(L, "couldn't dup file");
  }
  if(!shuso_set_log_fd(ctx, fd2)) {
    return luaL_error(L, ctx->errmsg);
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int Lua_shuso_set_error(lua_State *L) {
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

//----- === WATCHERS === -----

static const char *watchertype_str(shuso_lua_ev_watcher_type_t type) {
  switch(type) {
    case LUA_EV_WATCHER_IO:
      return "io";
    case LUA_EV_WATCHER_TIMER:
      return "timer";
    case LUA_EV_WATCHER_CHILD:
      return "child";
    case LUA_EV_WATCHER_SIGNAL:
      return "signal";
    default:
      return "???";
  }
}

static int Lua_watcher_gc(lua_State *L) {
  shuso_lua_ev_watcher_t *w = luaL_checkudata(L, 1, "shuttlesock.watcher");
  if(w->ref.self != LUA_NOREF) {
    luaL_unref(L, LUA_REGISTRYINDEX, w->ref.self);
    w->ref.self = LUA_NOREF;
  }
  
  lua_watcher_unref(L, w);
  return 0;
}
static void watcher_callback(struct ev_loop *loop, ev_watcher *watcher, int events) {
  shuso_lua_ev_watcher_t *w = watcher->data;
  shuso_t                *ctx = shuso_ev_ctx(loop, w);
  lua_State              *L = ctx->lua.state;
  lua_State              *coro = NULL;
  bool                    handler_is_coroutine;
  int                     rc;
  
  if(w->ref.handler == LUA_NOREF) {
    luaL_error(L, "no handler for shuttlesock io watcher");
    return;
  }
  
  lua_rawgeti(L, LUA_REGISTRYINDEX, w->ref.handler);
  handler_is_coroutine = lua_isthread(L, -1);
  if(!handler_is_coroutine) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, w->ref.self);
    lua_call(L, 1, 0);
    rc = LUA_OK;
  }
  else {
    coro = lua_tothread(L, -1);
    lua_rawgeti(coro, LUA_REGISTRYINDEX, w->ref.self);
    rc = shuso_lua_resume(coro, NULL, 1);
  }
  if((handler_is_coroutine && rc == LUA_OK) /* coroutine is finished */
   ||(w->type == LUA_EV_WATCHER_TIMER && w->watcher.timer.ev.repeat == 0.0) /* timer is finished */
  ) {
    lua_pushcfunction(L, Lua_watcher_stop);
    lua_rawgeti(L, LUA_REGISTRYINDEX, w->ref.self);
    lua_call(L, 1, 0);
    
    if(rc == LUA_YIELD) {
      luaL_error(coro ? coro : L, "watcher is finished, but the coroutine isn't");
    }
  }
}

static int Lua_watcher_set(lua_State *L) {
  shuso_t                *ctx = shuso_lua_ctx(L);
  int                     nargs = lua_gettop(L);
  shuso_lua_ev_watcher_t *w = luaL_checkudata(L, 1, "shuttlesock.watcher");
  int                     handler_index = 0;
  
  if(ev_is_active(&w->watcher.watcher) || ev_is_pending(&w->watcher.watcher)) {
    return luaL_error(L, "cannot call %s watcher:set(), the event is already active", watchertype_str(w->type));
  }
  
  switch(w->type) {
    case LUA_EV_WATCHER_IO: {
      if(nargs < 3) {
        return luaL_error(L, "io watcher:set() expects at least 2 arguments");
      }
      handler_index = 4;
      int fd = luaL_checkinteger(L, 2);
      int           events = 0;
      size_t        evstrlen;
      const char   *evstr = luaL_checklstring(L, 3, &evstrlen);
      if(evstrlen < 1 || evstrlen > 2) {
        return luaL_error(L, "invalid io watcher events string \"%s\"", evstr);
      }
      if(evstr[0]=='r' || evstr[1] == 'r') {
        events |= EV_READ;
      }
      if(evstr[0]=='w' || evstr[1] == 'w') {
        events |= EV_WRITE;
      }
      shuso_ev_io_init(ctx, &w->watcher.io, fd, events, (shuso_ev_io_fn *)watcher_callback, w);
    } break;
    
    case LUA_EV_WATCHER_TIMER: {
      if(nargs < 2) {
        return luaL_error(L, "timer watcher:set() expects at least 1 argument");
      }
      double after = luaL_checknumber(L, 2);
      double repeat;
      if(nargs == 3 && lua_isfunction(L, 3)) {
        //the [repeat] argument is optional, and may be followed by the handler
        handler_index = 3;
        repeat = 0.0;
      }
      else {
        handler_index = 4;
        repeat = luaL_optnumber(L, 3, 0.0);
      }
      shuso_ev_timer_init(ctx, &w->watcher.timer, after, repeat, (shuso_ev_timer_fn *)watcher_callback, w);
    } break;
    
    case LUA_EV_WATCHER_SIGNAL: {
      if(nargs < 2) {
        return luaL_error(L, "signal watcher:set() expects at least 1 argument");
      }
      int signum = luaL_checkinteger(L, 2);
      handler_index = 3;
      shuso_ev_signal_init(ctx, &w->watcher.signal, signum, (shuso_ev_signal_fn *)watcher_callback, w);
    } break;
    
    case LUA_EV_WATCHER_CHILD: {
      if(nargs < 2) {
        return luaL_error(L, "child watcher:set() expects at least 1 argument");
      }
      int pid = luaL_checkinteger(L, 2);
      int trace;
      if(nargs == 3 && lua_isfunction(L, 3)) {
        //the [trace] argument is optional, and may be followed by the handler
        handler_index = 3;
        trace = 0;
      }
      else {
        handler_index = 4;
        trace = luaL_optinteger(L, 3, 0);
      }
      shuso_ev_child_init(ctx, &w->watcher.child, pid, trace, (shuso_ev_child_fn *)watcher_callback, w);
    } break;
  }
  const char *handler_err = "last optional argument must be the handler. it's optional though, so you probably slapped an extra argument at the end that isn't a handler function";
  if(nargs > handler_index) {
    return luaL_error(L, handler_err);
  }
  
  if(lua_isfunction(L, handler_index)) {
    if(w->ref.handler != LUA_NOREF) {
      luaL_unref(L, LUA_REGISTRYINDEX, w->ref.handler);
    }
    w->ref.handler = luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else if(!lua_isnil(L, handler_index)) {
    return luaL_error(L, handler_err);
  }
  
  lua_pushvalue(L, 1);
  return 1;
}

static void lua_watcher_ref(lua_State *L, shuso_lua_ev_watcher_t *w, int watcher_index) {
  if(w->ref.self != LUA_NOREF) {
    luaL_error(L, "watcher already has a reference");
    return;
  }
  /*lua_pushliteral(L, "shuttlesock.watchers");
  if(lua_rawget(L, LUA_REGISTRYINDEX) != LUA_TTABLE) {
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "shuttlesock.watchers"); //pops "shuttlesock.watchers" table, but we pushed a copy of it so it's cool
  }
  
  lua_pushvalue(L, watcher_index);
  w->ref.self = luaL_ref(L, -2)
  */
  lua_pushvalue(L, watcher_index);
  w->ref.self = luaL_ref(L, LUA_REGISTRYINDEX);
  
  //cleanup
  lua_pop(L, 1);
}

static void lua_watcher_unref(lua_State *L, shuso_lua_ev_watcher_t *w) {
  if(w->ref.self == LUA_NOREF) {
    return;
  }
  /*
  lua_pushliteral(L, "shuttlesock.watchers");
  if(lua_rawget(L, LUA_REGISTRYINDEX) != LUA_TTABLE) {
    return;
  }
  
  luaL_unref(L, -1, w->ref.self);
  */
  luaL_unref(L, LUA_REGISTRYINDEX, w->ref.self);
  if(w->coroutine_thread) {
    w->coroutine_thread = NULL;
  }
  w->ref.self = LUA_NOREF;
  
  //cleanup
  lua_pop(L, 1);
}

static int Lua_watcher_start(lua_State *L) {
  shuso_t                *ctx = shuso_lua_ctx(L);
  shuso_lua_ev_watcher_t *w = luaL_checkudata(L, 1, "shuttlesock.watcher");
  if(ev_is_active(&w->watcher.watcher)) {
    return luaL_error(L, "shuttlesock.watcher already active");
  }
  if(w->ref.handler == LUA_NOREF) {
    return luaL_error(L, "shuttlesock.watcher cannot be started without a handler");
  }
  switch(w->type) {
    case LUA_EV_WATCHER_IO: 
      shuso_ev_io_start(ctx, &w->watcher.io);
      break;  
    case LUA_EV_WATCHER_TIMER:
      shuso_ev_timer_start(ctx, &w->watcher.timer);
      break;
    case LUA_EV_WATCHER_SIGNAL:
      shuso_ev_signal_start(ctx, &w->watcher.signal);
      break;
    case LUA_EV_WATCHER_CHILD:
      shuso_ev_child_start(ctx, &w->watcher.child);
      break;
  }
  lua_watcher_ref(L, w, 1);
  
  lua_pushvalue(L, 1);
  return 1;
}

static int Lua_watcher_stop(lua_State *L) {
  shuso_t                *ctx = shuso_lua_ctx(L);
  shuso_lua_ev_watcher_t *w = luaL_checkudata(L, 1, "shuttlesock.watcher");
  switch(w->type) {
    case LUA_EV_WATCHER_IO: 
      shuso_ev_io_stop(ctx, &w->watcher.io);
      break;  
    case LUA_EV_WATCHER_TIMER:
      shuso_ev_timer_stop(ctx, &w->watcher.timer);
      break;
    case LUA_EV_WATCHER_SIGNAL:
      shuso_ev_signal_stop(ctx, &w->watcher.signal);
      break;
    case LUA_EV_WATCHER_CHILD:
      shuso_ev_child_stop(ctx, &w->watcher.child);
      break;
  }
  lua_watcher_unref(L, w);
  lua_pushvalue(L, 1);
  return 1;
}

static int Lua_watcher_yield(lua_State *L) {
  shuso_lua_ev_watcher_t *w = luaL_checkudata(L, 1, "shuttlesock.watcher");
  if(w->coroutine_thread == L) {
    //yielding to the same coroutine as before
    assert(w->ref.handler != LUA_NOREF);
  }
  else if(w->ref.handler != LUA_NOREF) {
    if(lua_isthread(L, -1)) {
      luaL_error(L, "can't yield to shuttlesock.watcher, it's already yielding to a different coroutine");
    }
    else {
      luaL_error(L, "can't yield to shuttlesock.watcher, its handler has already been set");
    }
  }
  
  if(lua_pushthread(L) == 1) {
    return luaL_error(L, "cannot yield from main thread");
  }
  else if(!lua_isyieldable(L)) {
    return luaL_error(L, "cannot yield from here");
  }
  
  if(w->ref.handler == LUA_NOREF) {
    w->ref.handler = luaL_ref(L, LUA_REGISTRYINDEX);
    w->coroutine_thread = L;
  }
  else {
    assert(w->coroutine_thread == L);
  }
  
  lua_pushcfunction(L, Lua_watcher_start);
  lua_pushvalue(L, 1);
  lua_call(L, 1, 1);
  return lua_yield(L, 1);
}

static int Lua_watcher_newindex(lua_State *L) {
  shuso_lua_ev_watcher_t *w = luaL_checkudata(L, 1, "shuttlesock.watcher");
  const char             *field = luaL_checkstring(L, 2);
  
  //watcher.handler=(function)
  if(strcmp(field, "handler") == 0) {
    if(w->ref.handler != LUA_NOREF) {
      luaL_unref(L, LUA_REGISTRYINDEX, w->ref.handler);
      w->coroutine_thread = NULL;
    }
    if(lua_isfunction(L, -1)) {
      w->ref.handler = luaL_ref(L, LUA_REGISTRYINDEX);
      w->coroutine_thread = NULL;
    }
    else if(lua_isthread(L, -1)) {
      w->ref.handler = luaL_ref(L, LUA_REGISTRYINDEX);
      w->coroutine_thread = lua_tothread(L, -1);
      assert(w->coroutine_thread);
    }
    else {
      return luaL_error(L, "watcher handler must be a coroutine or function");
    }
  }
  
  //watcher.repeat=(float)
  else if(w->type == LUA_EV_WATCHER_TIMER && strcmp(field, "repeat") == 0) {
    double repeat = luaL_checknumber(L, -1);
    w->watcher.timer.ev.repeat = repeat;
  }
  
  else {
    return luaL_error(L, "don't know how to set shuttlesock %s watcher field \"%s\"", watchertype_str(w->type), field);
  }
  return 0;
}

static int Lua_watcher_index(lua_State *L) {
  shuso_lua_ev_watcher_t *w = luaL_checkudata(L, 1, "shuttlesock.watcher");
  const char             *field = luaL_checkstring(L, 2);
  
  if(strcmp(field, "set") == 0) {
    lua_pushcfunction(L, Lua_watcher_set);
    return 1;
  }
  else if(strcmp(field, "start") == 0) {
    lua_pushcfunction(L, Lua_watcher_start);
    return 1;
  }
  else if(strcmp(field, "yield") == 0) {
    lua_pushcfunction(L, Lua_watcher_yield);
    return 1;
  }
  else if(strcmp(field, "stop") == 0) {
    lua_pushcfunction(L, Lua_watcher_stop);
    return 1;
  }
  else if(strcmp(field, "active") == 0) {
    lua_pushboolean(L, ev_is_active(&w->watcher.watcher) || ev_is_pending(&w->watcher.watcher));
    return 1;
  }
  
  else if(strcmp(field, "pending") == 0) {
    lua_pushboolean(L, ev_is_pending(&w->watcher.watcher));
    return 1;
  }
  
  else if(strcmp(field, "handler") == 0) {
    if(w->ref.handler == LUA_NOREF) {
      lua_pushnil(L);
    }
    else {
      lua_rawgeti(L, LUA_REGISTRYINDEX, w->ref.handler);
    }
    return 1;
  }
  
  else if(w->type == LUA_EV_WATCHER_IO){
    if(strcmp(field, "fd") == 0) {
      lua_pushinteger(L, w->watcher.io.ev.fd);
    }
    else if(strcmp(field, "events") == 0) {
      int evts = w->watcher.io.ev.events;
      if(evts & (EV_READ | EV_WRITE)) {
        lua_pushliteral(L, "rw");
      }
      else if(evts & EV_READ) {
        lua_pushliteral(L, "r");
      }
      else if(evts & EV_WRITE) {
        lua_pushliteral(L, "w");
      }
      else {
        lua_pushliteral(L, "");
      }
    }
    return 1;
  }
  
  else if(w->type == LUA_EV_WATCHER_TIMER) {
    if(strcmp(field, "repeat") == 0) {
      lua_pushnumber(L, w->watcher.timer.ev.repeat);
    }
    else if(strcmp(field, "after") == 0) {
      lua_pushnumber(L, ((ev_watcher_time *)&w->watcher.timer.ev)->at);
    }
    return 1;
  }
  
  else if(w->type == LUA_EV_WATCHER_SIGNAL) {
    if(strcmp(field, "signum") == 0) {
      lua_pushinteger(L, w->watcher.signal.ev.signum);
    }
    return 1;
  }
  
  else if(w->type == LUA_EV_WATCHER_CHILD) {
    if(strcmp(field, "pid") == 0) {
      lua_pushinteger(L, w->watcher.child.ev.pid);
    }
    else if(strcmp(field, "rpid") == 0) {
      lua_pushinteger(L, w->watcher.child.ev.rpid);
    }
    else if(strcmp(field, "rstatus") == 0) {
      lua_pushinteger(L, w->watcher.child.ev.rstatus);
    }
    return 1;
  }
  
  return luaL_error(L, "unknown field \"%s\" for shuttlesock %s watcher", field, watchertype_str(w->type));
}

int Lua_shuso_new_watcher(lua_State *L) {
  const char                   *type = luaL_checkstring(L, 1);
  shuso_lua_ev_watcher_type_t  wtype;
  int                          nargs = lua_gettop(L);
  
  if(strcmp(type, "io")==0) {
    wtype = LUA_EV_WATCHER_IO;
  }
  else if(strcmp(type, "timer") == 0) {
    wtype = LUA_EV_WATCHER_TIMER;
  }
  else if(strcmp(type, "child") == 0) {
    wtype = LUA_EV_WATCHER_CHILD;
  }
  else if(strcmp(type, "signal") == 0) {
    wtype = LUA_EV_WATCHER_SIGNAL;
  }
  else {
    return luaL_error(L, "invalid watcher type \"%s\"", type);
  }
  
  shuso_lua_ev_watcher_t *watcher;
  if((watcher = lua_newuserdata(L, sizeof(*watcher))) == NULL) {
    return luaL_error(L, "unable to allocate memory for new watcher");
  }
  
  memset(&watcher->watcher, 0, sizeof(watcher->watcher));
  watcher->type = wtype;
  watcher->ref.handler = LUA_NOREF;
  watcher->ref.self = LUA_NOREF;
  
  if(luaL_newmetatable(L, "shuttlesock.watcher")) {
    lua_pushcfunction(L, Lua_watcher_gc);
    lua_setfield(L, -2, "__gc");
    
    lua_pushcfunction(L, Lua_watcher_index);
    lua_setfield(L, -2, "__index");
    
    lua_pushcfunction(L, Lua_watcher_newindex);
    lua_setfield(L, -2, "__newindex");
  }
  lua_setmetatable(L, -2);
  
  if(nargs > 1) {
    lua_replace(L, 1);
    return Lua_watcher_set(L);
  }
  
  ev_init(&watcher->watcher.watcher, NULL);
  return 1;
}

//shared memory slab

static int Lua_shared_string_tostring(lua_State *L) {
  shuso_lua_shared_string_t **shstr = luaL_checkudata(L, 1, "shuttlesock.shared_string");
  if(*shstr == NULL) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushlstring(L, (*shstr)->data, (*shstr)->len);
  return 1;
}
static int Lua_shuso_shared_slab_alloc_string(lua_State *L) {
  shuso_t    *ctx = shuso_lua_ctx(L);
  size_t      len;
  const char *str = luaL_checklstring(L, 1, &len);
  shuso_lua_shared_string_t **shstr;
  if((shstr = lua_newuserdata(L, sizeof(shstr))) == NULL) {
    return luaL_error(L, "unable to allocate memory for new shared string");
  }
  if((*shstr = shuso_shared_slab_alloc(&ctx->common->shm, sizeof(shuso_lua_shared_string_t) + len)) == NULL) {
    return luaL_error(L, "unable to allocate shared memory of size %d for new shared string", (int )len);
  }
  (*shstr)->len = len;
  memcpy((void *)(*shstr)->data, str, len);
  
  if(luaL_newmetatable(L, "shuttlesock.shared_string")) {
    //lua_pushcfunction(L, Lua_shared_string_gc);
    //lua_setfield(L, -2, "__gc");
    
    lua_pushcfunction(L, Lua_shared_string_tostring);
    lua_setfield(L, -2, "__tostring");
  }
  
  lua_setmetatable(L, -2);
  return 1;
}
static int Lua_shuso_shared_slab_free_string(lua_State *L) {
  shuso_lua_shared_string_t **shstr = luaL_checkudata(L, 1, "shuttlesock.shared_string");
  shuso_t                    *ctx = shuso_lua_ctx(L);
  if(*shstr == NULL) {
    lua_pushnil(L);
    lua_pushliteral(L, "shuttlesock.shared_string already freed");
    return 2;
  }
  shuso_shared_slab_free(&ctx->common->shm, *shstr);
  *shstr = NULL;
  lua_pushboolean(L, 1);
  return 1;
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
  {"newWatcher", Lua_shuso_new_watcher},
  
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
