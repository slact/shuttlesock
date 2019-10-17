#include <shuttlesock.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <unistd.h>
#include <strings.h>
#include <arpa/inet.h>
#include <ares.h>
#include <errno.h>

typedef enum {
  LUA_EV_WATCHER_IO =       0,
  LUA_EV_WATCHER_TIMER =    1,
  LUA_EV_WATCHER_CHILD =    2,
  LUA_EV_WATCHER_SIGNAL =   3
} shuso_lua_ev_watcher_type_t;

struct shuso_lua_ev_watcher_s {
  shuso_ev_any      watcher;
  struct {
    int               self;
    int               handler;
  }                 ref;
  unsigned          type:4;
  lua_State        *coroutine_thread;
}; // shuso_lua_ev_watcher_t

typedef struct {
  struct {
    int               self;
    int               handler;
  }                 ref;
  lua_State        *coroutine_thread;
} shuso_lua_fd_receiver_t;

static int Lua_watcher_stop(lua_State *L);
static void lua_watcher_unref(lua_State *L, shuso_lua_ev_watcher_t *w);
static const char *watchertype_str(shuso_lua_ev_watcher_type_t type);

static void lua_get_registry_table(lua_State *L, const char *name) {
  lua_getfield(L, LUA_REGISTRYINDEX, name);
  if(lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, name);
  }
}

typedef struct {
  size_t      len;
  const char  data[];
} shuso_lua_shared_string_t;

static bool lua_push_handler_function_or_coroutine(lua_State *L, int nargs, lua_State **coroutine, bool caller_can_be_handler, bool allow_no_handler) {
//is there a handler as the last argument? no? then is the caller a yieldable coroutine?
  lua_State *coro = NULL;
  int type = nargs > 0 ? lua_type(L, nargs) : LUA_TNIL;
  if(type == LUA_TFUNCTION) {
    if(coroutine) *coroutine = NULL;
    lua_pushvalue(L, nargs);
    return true;
  }
  else if(type == LUA_TTHREAD) {
    coro = lua_tothread(L, nargs);
    lua_pushvalue(L, nargs);
  }
  else if(caller_can_be_handler) {
    coro = L;
    if(lua_pushthread(L) == 1) {
      lua_pop(L, 1);
      if(!allow_no_handler) {
        return luaL_error(L, "handler can't be the main thread because it's not a coroutine and can't yield and spindly spiders");
      }
    }
  }
  
  if(!coro && allow_no_handler) {
    if(coroutine) *coroutine = NULL;
    return false;
  }
  
  if(!lua_isyieldable(coro)) {
    if(coroutine) *coroutine = NULL;
    if(!allow_no_handler) {
      return luaL_error(L, "handler coroutine isn't yieldable");
    }
    return false;
  }
  
  //reference coroutine
  if(coroutine) {
    *coroutine = coro;
  }
  return true;
}

static int lua_ref_handler_function_or_coroutine(lua_State *L, int nargs, lua_State **coroutine, bool caller_can_be_handler, bool allow_no_handler) {
//is there a handler as the last argument? no? then is the caller a yieldable coroutine?
  if(lua_push_handler_function_or_coroutine(L, nargs, coroutine, caller_can_be_handler, allow_no_handler)) {
    return luaL_ref(L, LUA_REGISTRYINDEX);
  }
  return LUA_NOREF;
}

static int lua_push_nil_error(lua_State *L) {
  shuso_t *S = shuso_state(L);
  lua_pushnil(L);
  const char *errmsg = shuso_last_error(S);
  lua_pushstring(L, errmsg == NULL ? "no error" : errmsg);
  return 2;
}

static void lua_getlib_field(lua_State *L, const char *lib, const char *field) {
  lua_getglobal(L, lib);
  assert(lua_istable(L, -1));
  lua_getfield(L, -1, field);
  lua_remove(L, -2);
}

//create a shuttlesock instance from inside Lua
static int Lua_shuso_create(lua_State *L) {
  if(shuso_state(L)) {
    return luaL_error(L, "shuttlesock instance already exists");
  }
  const char  *err;
  shuso_t     *S = shuso_create(&err);
  if(!S) {
    lua_pushnil(L);
    lua_pushstring(L, err);
    return 2;
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int Lua_shuso_configure_file(lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  shuso_t    *S = shuso_state(L);
  if(!shuso_configure_file(S, path)) {
    return lua_push_nil_error(L);
  }
  lua_pushboolean(L, 1);
  return 1;
}
static int Lua_shuso_configure_string(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  const char *string = luaL_checkstring(L, 2);
  shuso_t    *S = shuso_state(L);
  if(!shuso_configure_string(S, name, string)) {
    return lua_push_nil_error(L);
  }
  lua_pushboolean(L, 1);
  return 1;
}



static int Lua_shuso_configure_finish(lua_State *L) {
  shuso_t *S = shuso_state(L);
  if(!shuso_configure_finish(S)) {
    return luaS_shuso_error(L);
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int Lua_shuso_destroy(lua_State *L) {
  shuso_t *S = shuso_state(L);
  if(!shuso_destroy(S)) {
    return luaS_shuso_error(L);
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int Lua_shuso_run(lua_State *L) {
  shuso_t *S = shuso_state(L);
  if(!shuso_run(S)) {
    return luaS_shuso_error(L);
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
  shuso_t       *S = shuso_state(L);
  shuso_stop_t   lvl = stop_level_string_arg_to_enum(L, "ask", 1);
  if(!shuso_stop(S, lvl)) {
    return luaS_shuso_error(L);
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int Lua_shuso_runstate(lua_State *L) {
  shuso_t *S = shuso_state(L);
  luaS_push_runstate(L, S->common->state);
  return 1;
}
static int Lua_shuso_process_runstate(lua_State *L) {
  shuso_t *S = shuso_state(L);
  if(lua_gettop(L) == 0) {
    luaS_push_runstate(L, *S->process->state);
    return 1;
  }
  if(lua_isstring(L, 1)) {
    const char *str = lua_tostring(L, 1);
    if(strcmp(str, "master") == 0) {
      lua_pop(L, 1);
      lua_pushinteger(L, SHUTTLESOCK_MASTER);
    }
    else if(strcmp(str, "manager") == 0) {
      lua_pop(L, 1);
      lua_pushinteger(L, SHUTTLESOCK_MANAGER);
    }
    else {
      lua_pushnil(L);
      lua_pushfstring(L, "unknown process \"%s\"", str);
      return 2;
    }
  }
  if(!lua_isinteger(L, 1)) {
    //not an int or string
    lua_getglobal(L, "tostring");
    lua_pushvalue(L, 1);
    lua_pcall(L, 1, 1, 0);
    lua_pushnil(L);
    lua_pushfstring(L, "invalid process %s", lua_tostring(L, -2));
    return 2;
  }
  int procnum = lua_tointeger(L, 1);
  switch(procnum) {
    case SHUTTLESOCK_MASTER:
      luaS_push_runstate(L, *S->common->process.master.state);
      return 1;
    case SHUTTLESOCK_MANAGER:
      luaS_push_runstate(L, *S->common->process.master.state);
      return 1;
    default:
      if(procnum < SHUTTLESOCK_MASTER) {
        lua_pushnil(L);
        lua_pushfstring(L, "invalid process number %d", procnum);
        return 2;
      }
      if(procnum > SHUTTLESOCK_MANAGER && (procnum < S->common->process.workers_start || procnum > S->common->process.workers_end)) {
        lua_pushnil(L);
        lua_pushfstring(L, "inactive worker number %d", procnum);
        return 2;
      }
      luaS_push_runstate(L, *S->common->process.worker[procnum].state);
      return 1;
  }
}
/*
static int Lua_shuso_spawn_manager(lua_State *L) {
  shuso_t *S = shuso_state(L);
  if(!shuso_spawn_manager(S)) {
    return luaS_shuso_error(L);
  }
  lua_pushboolean(L, 1);
  return 1;
}
static int Lua_shuso_stop_manager(lua_State *L) {
  shuso_t      *S = shuso_state(L);
  shuso_stop_t  lvl = stop_level_string_arg_to_enum(L, "ask", 1);
  if(!shuso_stop_manager(S, lvl)) {
    return luaS_shuso_error(L);
  }
  lua_pushboolean(L, 1);
  return 1;
}
static int Lua_shuso_spawn_worker(lua_State *L) {
  shuso_t    *S = shuso_state(L);
  int         workernum = S->common->process.workers_end;
  shuso_process_t   *proc = &S->common->process.worker[workernum];
  if(!shuso_spawn_worker(S, proc)) {
    return luaS_shuso_error(L);
  }
  lua_pushboolean(L, 1);
  return 1;
}
static int Lua_shuso_stop_worker(lua_State *L) {
  shuso_t     *S = shuso_state(L);
  int          workernum = luaL_checkinteger(L, 1);
  if(workernum < S->common->process.workers_start || workernum > S->common->process.workers_end) {
    return luaL_error(L, "invalid worker %d (valid range: %d-%d)", workernum, S->common->process.workers_start, S->common->process.workers_end);
  }
  shuso_process_t   *proc = &S->common->process.worker[workernum];
  shuso_stop_t       lvl = stop_level_string_arg_to_enum(L, "ask", 2);
  if(!shuso_stop_worker(S, proc, lvl)) {
    return luaS_shuso_error(L);
  }
  S->common->process.workers_end++;
  lua_pushboolean(L, 1);
  return 1;
}*/

static int Lua_shuso_set_log_fd(lua_State *L) {
  shuso_t       *S = shuso_state(L);
  luaL_Stream   *io = luaL_checkudata(L, 1, LUA_FILEHANDLE);
  int fd = fileno(io->f);
  if(fd == -1) {
    return luaL_error(L, "invalid file");
  }
  int fd2 = dup(fd);
  if(fd2 == -1) {
    return luaL_error(L, "couldn't dup file");
  }
  if(!shuso_set_log_fd(S, fd2)) {
    return luaS_shuso_error(L);
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int Lua_shuso_set_error(lua_State *L) {
  int            nargs = lua_gettop(L);
  shuso_t       *S = shuso_state(L);
  luaL_checkstring(L, 1);
  
  lua_getlib_field(L, "string", "format");
  lua_call(L, nargs, 1);
  
  const char    *err = lua_tostring(L, -1);
  shuso_set_error(S, err);
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
  shuso_t                *S = shuso_state(loop, watcher);
  shuso_lua_ev_watcher_t *w = watcher->data;
  lua_State              *L = S->lua.state;
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
    rc = luaS_resume(coro, NULL, 1);
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
  shuso_t                *S = shuso_state(L);
  int                     nargs = lua_gettop(L);
  shuso_lua_ev_watcher_t *w = luaL_checkudata(L, 1, "shuttlesock.watcher");
  
  if(ev_is_active(&w->watcher.watcher) || ev_is_pending(&w->watcher.watcher)) {
    return luaL_error(L, "cannot call %s watcher:set(), the event is already active", watchertype_str(w->type));
  }
  
  switch(w->type) {
    case LUA_EV_WATCHER_IO: {
      if(nargs-1 < 2 || nargs-1 > 3) {
        return luaL_error(L, "io watcher:set() expects 2-3 arguments");
      }
      int fd = luaL_checkinteger(L, 2);
      int           events = 0;
      size_t        evstrlen;
      const char   *evstr = luaL_checklstring(L, 3, &evstrlen);
      if(strchr(evstr, 'r')) {
        events |= EV_READ;
      }
      if(strchr(evstr, 'w')) {
        events |= EV_WRITE;
      }
      if((events & (EV_READ | EV_WRITE)) == 0) {
        return luaL_error(L, "invalid io watcher events string \"%s\"", evstr);
      }
      shuso_ev_io_init(S, &w->watcher.io, fd, events, (shuso_ev_io_fn *)watcher_callback, w);
    } break;
    
    case LUA_EV_WATCHER_TIMER: {
      if(nargs-1 < 1 || nargs-1 > 2) {
        return luaL_error(L, "timer watcher:set() expects 1-2 arguments");
      }
      double after = luaL_checknumber(L, 2);
      double repeat;
      if(nargs-1 == 2 && (lua_isfunction(L, 3) || lua_isthread(L, 3))) {
        //the [repeat] argument is optional, and may be followed by the handler
        repeat = 0.0;
      }
      else {
        repeat = luaL_optnumber(L, 3, 0.0);
      }
      shuso_ev_timer_init(S, &w->watcher.timer, after, repeat, (shuso_ev_timer_fn *)watcher_callback, w);
    } break;
    
    case LUA_EV_WATCHER_SIGNAL: {
      if(nargs-1 < 1 || nargs-1>2) {
        return luaL_error(L, "signal watcher:set() expects 1-2 arguments");
      }
      int signum = luaL_checkinteger(L, 2);
      shuso_ev_signal_init(S, &w->watcher.signal, signum, (shuso_ev_signal_fn *)watcher_callback, w);
    } break;
    
    case LUA_EV_WATCHER_CHILD: {
      if(nargs-1 < 2 || nargs-1 > 3) {
        return luaL_error(L, "child watcher:set() expects at least 1 argument");
      }
      int pid = luaL_checkinteger(L, 2);
      int trace;
      if(nargs-1 == 2 && (lua_isfunction(L, 3) || lua_isthread(L, 3))) {
        //the [trace] argument is optional, and may be followed by the handler
        trace = 0;
      }
      else {
        trace = luaL_checkinteger(L, 3);
      }
      shuso_ev_child_init(S, &w->watcher.child, pid, trace, (shuso_ev_child_fn *)watcher_callback, w);
    } break;
  }
  
  int handler_ref = lua_ref_handler_function_or_coroutine(L, nargs, NULL, false, true);
  
  if(handler_ref != LUA_NOREF) {
    if(w->ref.handler != LUA_NOREF) {
      luaL_unref(L, LUA_REGISTRYINDEX, w->ref.handler);
    }
    w->ref.handler = handler_ref;
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
  shuso_t                *S = shuso_state(L);
  shuso_lua_ev_watcher_t *w = luaL_checkudata(L, 1, "shuttlesock.watcher");
  if(ev_is_active(&w->watcher.watcher)) {
    return luaL_error(L, "shuttlesock.watcher already active");
  }
  if(w->ref.handler == LUA_NOREF) {
    return luaL_error(L, "shuttlesock.watcher cannot be started without a handler");
  }
  switch(w->type) {
    case LUA_EV_WATCHER_IO: 
      shuso_ev_io_start(S, &w->watcher.io);
      break;  
    case LUA_EV_WATCHER_TIMER:
      shuso_ev_timer_start(S, &w->watcher.timer);
      break;
    case LUA_EV_WATCHER_SIGNAL:
      shuso_ev_signal_start(S, &w->watcher.signal);
      break;
    case LUA_EV_WATCHER_CHILD:
      shuso_ev_child_start(S, &w->watcher.child);
      break;
  }
  lua_watcher_ref(L, w, 1);
  
  lua_pushvalue(L, 1);
  return 1;
}

static int Lua_watcher_stop(lua_State *L) {
  shuso_t                *S = shuso_state(L);
  shuso_lua_ev_watcher_t *w = luaL_checkudata(L, 1, "shuttlesock.watcher");
  switch(w->type) {
    case LUA_EV_WATCHER_IO: 
      shuso_ev_io_stop(S, &w->watcher.io);
      break;  
    case LUA_EV_WATCHER_TIMER:
      shuso_ev_timer_stop(S, &w->watcher.timer);
      break;
    case LUA_EV_WATCHER_SIGNAL:
      shuso_ev_signal_stop(S, &w->watcher.signal);
      break;
    case LUA_EV_WATCHER_CHILD:
      shuso_ev_child_stop(S, &w->watcher.child);
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
      return luaL_error(L, "can't yield to shuttlesock.watcher, it's already yielding to a different coroutine");
    }
    else {
      return luaL_error(L, "can't yield to shuttlesock.watcher, its handler has already been set");
    }
  }
  
  assert(w->ref.handler == LUA_NOREF);
  assert(w->coroutine_thread == NULL);
  w->ref.handler = lua_ref_handler_function_or_coroutine(L, 0, &w->coroutine_thread, true, false);
  assert(w->coroutine_thread);
  assert(w->ref.handler != LUA_NOREF);
  
  lua_pushcfunction(w->coroutine_thread, Lua_watcher_start);
  lua_pushvalue(w->coroutine_thread, 1);
  lua_call(w->coroutine_thread, 1, 1);
  return lua_yield(w->coroutine_thread, 1);
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
    if(lua_isnil(L, 3)) {
      return 0;
    }
    w->ref.handler = lua_ref_handler_function_or_coroutine(L, 3, &w->coroutine_thread, false, false);
    if(w->ref.handler == LUA_NOREF) {
      assert(w->coroutine_thread == NULL);
      return luaL_error(L, "watcher handler must be a coroutine or function");
    }
  }
  //watcher.repeat=(float)
  else if(w->type == LUA_EV_WATCHER_TIMER && strcmp(field, "repeat") == 0) {
    double repeat = luaL_checknumber(L, 3);
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
    lua_rawgeti(L, LUA_REGISTRYINDEX, w->ref.handler);
    //TODO: check that LUA_REGISTRYINDEX[LUA_NOREF] == nil
    return 1;
  }
  else if(strcmp(field, "type") == 0) {
    lua_pushstring(L, watchertype_str(w->type));
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
      union { //stop type-punning warning from complaining
        ev_timer *ev;
        ev_watcher_time *watcher;
      } ww = { .ev = &w->watcher.timer.ev }; 
      lua_pushnumber(L, ww.watcher->at);
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
  
  if(strcmp(type, "io") == 0) {
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
  
  lua_replace(L, 1);
  
  lua_pushcfunction(L, Lua_watcher_set);
  lua_insert(L, 1);
  lua_call(L, nargs, 1);
  
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
  shuso_t    *S = shuso_state(L);
  size_t      len;
  const char *str = luaL_checklstring(L, 1, &len);
  shuso_lua_shared_string_t **shstr;
  if((shstr = lua_newuserdata(L, sizeof(*shstr))) == NULL) {
    return luaL_error(L, "unable to allocate memory for new shared string");
  }
  if((*shstr = shuso_shared_slab_alloc(&S->common->shm, sizeof(shuso_lua_shared_string_t) + len)) == NULL) {
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
  shuso_t                    *S = shuso_state(L);
  if(*shstr == NULL) {
    lua_pushnil(L);
    lua_pushliteral(L, "shuttlesock.shared_string already freed");
    return 2;
  }
  shuso_shared_slab_free(&S->common->shm, *shstr);
  *shstr = NULL;
  lua_pushboolean(L, 1);
  return 1;
}

//resolver

static void resolve_hostname_callback(shuso_t *, shuso_resolver_result_t, struct hostent *, void *);

static int Lua_shuso_resolve_hostname(lua_State *L) {
  int         nargs = lua_gettop(L);
  const char *name = luaL_checkstring(L, 1);
  const char *addr_family_str = "IPv4";
  int         addr_family;
  
  if(nargs > 1 && lua_type(L, 2) != LUA_TFUNCTION && lua_type(L, 2) != LUA_TTHREAD) {
    addr_family_str = luaL_checkstring(L, 2);
  }
  
  if(strcasecmp(addr_family_str, "IPv4") == 0 || strcasecmp(addr_family_str, "INET") == 0) {
    addr_family = AF_INET;
  }
  else if(strcasecmp(addr_family_str, "IPv6") == 0 || strcasecmp(addr_family_str, "INET6") == 0) {
#ifdef AF_INET6
    addr_family = AF_INET6;
#else
    return luaL_error(L, "shuttlesock.resolve cannot handle IPv6 because it's not enabled on this system");
#endif
  }
  else {
    return luaL_error(L, "shuttlesock.resolve unknown address family \"%s\"", addr_family_str);
  }
  
  shuso_t    *S = shuso_state(L);
  lua_State  *coro = NULL;
  int         handler_ref = lua_ref_handler_function_or_coroutine(L, nargs, &coro, true, false);;
  assert(handler_ref != LUA_NOREF);
  
  shuso_resolve_hostname(&S->resolver, name, addr_family, resolve_hostname_callback, (void *)(intptr_t)handler_ref);
  
  if(coro) {
    lua_pushboolean(coro, 1);
    return lua_yield(coro, 1);
  }
  else {
    lua_pushboolean(L, 1);
    return 1;
  }
}

static void resolve_hostname_callback(shuso_t *S, shuso_resolver_result_t result, struct hostent *hostent, void *pd) {
  lua_State     *L = S->lua.state;
  int            handler_ref = (intptr_t )pd;
  
  lua_rawgeti(L, LUA_REGISTRYINDEX, handler_ref);
  luaL_unref(L, LUA_REGISTRYINDEX, handler_ref);
  if(result != SHUSO_RESOLVER_SUCCESS) {
    lua_pushnil(L);
    switch(result) {
      case SHUSO_RESOLVER_FAILURE:
        lua_pushliteral(L, "failed to resolve name");
        break;
      case SHUSO_RESOLVER_FAILURE_NOTIMP:
        lua_pushliteral(L, "failed to resolve name: address family not implemented");
        break;
      case SHUSO_RESOLVER_FAILURE_BADNAME:
        lua_pushliteral(L, "failed to resolve name: invalid name");
        break;
      case SHUSO_RESOLVER_FAILURE_NODATA:
        lua_pushliteral(L, "failed to resolve name: no data from DNS server");
        break;
      case SHUSO_RESOLVER_FAILURE_NOTFOUND:
        lua_pushliteral(L, "failed to resolve name: not found");
        break;
      case SHUSO_RESOLVER_FAILURE_NOMEM:
        lua_pushliteral(L, "failed to resolve name: out of memory");
        break;
      case SHUSO_RESOLVER_FAILURE_CANCELLED:
        lua_pushliteral(L, "failed to resolve name: request cancelled");
        break;
      case SHUSO_RESOLVER_FAILURE_CONNREFUSED:
        lua_pushliteral(L, "failed to resolve name: connection refused");
        break;
      case SHUSO_RESOLVER_SUCCESS:
        //for enumeration purposes
        break;
    }
    lua_pushinteger(L, result);
    luaS_call_or_resume(L, 3);
    return;
  }
  
  if(!hostent) {
    lua_pushnil(L);
    lua_pushliteral(L, "failed to resolve name: missing hostent struct");
    lua_pushinteger(L, SHUSO_RESOLVER_FAILURE);
    luaS_call_or_resume(L, 3);
    return;
  }
  
  char  **pstr;
#ifdef INET6_ADDRSTRLEN
  char  address_str[INET6_ADDRSTRLEN];
#else
  char  address_str[INET_ADDRSTRLEN];
#endif  
  int   i;
  
  lua_newtable(L);
  
  //hostname
  lua_pushstring(L, hostent->h_name);
  lua_setfield(L, -2, "name");
  
  //aliases
  lua_newtable(L);
  i = 1;
  for (pstr = hostent->h_aliases; *pstr != NULL; pstr++) {
    lua_pushstring(L, *pstr);
    lua_rawseti(L, -2, i++);
  }
  lua_setfield(L, -2, "aliases");
  
  switch(hostent->h_addrtype) {
    case AF_INET:
      lua_pushliteral(L, "IPv4");
      break;
#ifdef AF_INET6
    case AF_INET6:
      lua_pushliteral(L, "IPv6");
      break;
#endif
    default:
      lua_pushliteral(L, "unknown");
      break;
  }
  lua_setfield(L, -2, "addrtype");
  
  lua_pushinteger(L, hostent->h_length);
  lua_setfield(L, -2, "length");
  
  i=1;
  lua_newtable(L); //binary addresses
  lua_newtable(L); //text addresses
  for(pstr = hostent->h_addr_list; *pstr != NULL; pstr++) {
    if(inet_ntop(hostent->h_addrtype, *pstr, address_str, sizeof(address_str)) == NULL) {
      luaL_error(L, "inet_ntop failed on address in list. this is very weird");
      return;
    }
    lua_pushstring(L, address_str);
    lua_rawseti(L, -2, i);
    lua_pushlstring(L, *pstr, hostent->h_length);
    lua_rawseti(L, -3, i);
    i++;
  }
  lua_setfield(L, -3, "addresses");
  lua_setfield(L, -2, "addresses_binary");
  
  if(inet_ntop(hostent->h_addrtype, hostent->h_addr, address_str, sizeof(address_str)) == NULL) {
    luaL_error(L, "inet_ntop failed on address. this is very weird");
    return;
  }
  lua_pushstring(L, address_str);
  lua_setfield(L, -2, "address");
  
  lua_pushlstring(L, hostent->h_addr, hostent->h_length);
  lua_setfield(L, -2, "address_binary");
  
  luaS_call_or_resume(L, 1);
}

//logger
static int log_internal(lua_State *L, void (*logfunc)(shuso_t *S, const char *fmt, ...)) {
  int            nargs = lua_gettop(L);
  shuso_t       *S = shuso_state(L);
  luaL_checkstring(L, 1);
  
  lua_getlib_field(L, "string", "format");
  lua_call(L, nargs, 1);
  logfunc(S, "%s", lua_tostring(L, -1));
  lua_pushboolean(L, 1);
  return 1;
}
static int Lua_shuso_log(lua_State *L) {
  return log_internal(L, &shuso_log);
}
static int Lua_shuso_log_debug(lua_State *L) {
  return log_internal(L, &shuso_log_debug);
}
static int Lua_shuso_log_info(lua_State *L) {
  return log_internal(L, &shuso_log_info);
}
static int Lua_shuso_log_notice(lua_State *L) {
  return log_internal(L, &shuso_log_notice);
}
static int Lua_shuso_log_warning(lua_State *L) {
  return log_internal(L, &shuso_log_warning);
}
static int Lua_shuso_log_error(lua_State *L) {
  return log_internal(L, &shuso_log_error);
}
static int Lua_shuso_log_critical(lua_State *L) {
  return log_internal(L, &shuso_log_critical);
}
static int Lua_shuso_log_fatal(lua_State *L) {
  return log_internal(L, &shuso_log_fatal);
}

//ipc
static shuso_process_t *lua_shuso_checkprocnum(lua_State *L, int index) {
  int       type = lua_type(L, index);
  shuso_process_t *proc;
  shuso_t  *S = shuso_state(L);
  
  if(type == LUA_TSTRING) {
    const char *str = lua_tostring(L, index);
    if(strcmp(str, "master")==0) {
      proc = &S->common->process.master;
    }
    else if(strcmp(str, "manager")==0) {
      proc = &S->common->process.manager;
    }
    else {
      luaL_error(L, "process string must be 'master' or 'manager', or integer number of the worker");
      return NULL;
    }
  }
  else if(type == LUA_TNUMBER) {
    int procnum = lua_tointeger(L, index);
    if(procnum <= SHUTTLESOCK_NOPROCESS) {
      luaL_error(L, "invalid procnum");
      return NULL;
    }
    else if(procnum == SHUTTLESOCK_MASTER) {
      proc = &S->common->process.master;
    }
    else if(procnum == SHUTTLESOCK_MANAGER) {
      proc = &S->common->process.manager;
    }
    else if(procnum >= SHUTTLESOCK_WORKER) {
      if(procnum < S->common->process.workers_start || procnum > S->common->process.workers_end) {
        luaL_error(L, "invalid worker number %d, must be between %d and %d", procnum, (int)S->common->process.workers_start, (int)S->common->process.workers_end);
        return NULL;
      }
      proc = &S->common->process.worker[procnum];
    }
    else {
      raise(SIGABRT); // how did we get here?... these clauses should have covered the entire range.
      return NULL;
    }
  }
  else {
    luaL_error(L, "procnum must be a number or string");
    return NULL;
  }
  return proc;
}

static int Lua_shuso_ipc_send_fd(lua_State *L) {
  int              nargs = lua_gettop(L);
  shuso_process_t *proc = lua_shuso_checkprocnum(L, 1);
  int              fd = luaL_checkinteger(L, 2);
  uintptr_t        ref;
  if(nargs < 3) {
    return luaL_error(L, "not enough arguments");
  }
  if(lua_isnumber(L, 3)) {
    lua_Integer num = luaL_checkinteger(L, 3);
    if(num < 0) {
      return luaL_error(L, "ref must be non-negative");
    }
    ref = num;
  }
  else {
    return luaL_error(L, "ref must be a number");
  }

  //size_t           strlen = 0;
  //const char      *str = nargs >= 4 ? luaL_checklstring(L, 4, &strlen);
  //TODO: support sending privdata along with the fd
  
  shuso_t *S = shuso_state(L);
  bool ok = shuso_ipc_send_fd(S, proc, fd, ref, NULL);
  if(!ok) {
    return luaS_shuso_error(L);
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int Lua_ipc_file_receiver_gc(lua_State *L) {
  return 0;
}
static int Lua_ipc_file_receiver_index(lua_State *L) {
  return 0;
}
static int Lua_ipc_file_receiver_newindex(lua_State *L) {
  return 0;
}
static int Lua_shuso_ipc_file_receiver_new(lua_State *L) {
  //int           nargs = lua_gettop(L);
  //uintptr_t     fd_receiver_ref = luaL_checkinteger(L, 1);
  //const char   *description = luaL_checkstring(L, 2);
  //double        timeout_sec = luaL_checknumber(L, 3);
  
  shuso_lua_fd_receiver_t *receiver;
  if((receiver = lua_newuserdata(L, sizeof(*receiver))) == NULL) {
    return luaL_error(L, "unable to allocate memory for new file receiver");
  }
  
  if(luaL_newmetatable(L, "shuttlesock.ipc.fd_receiver")) {
    lua_pushcfunction(L, Lua_ipc_file_receiver_gc);
    lua_setfield(L, -2, "__gc");
    
    lua_pushcfunction(L, Lua_ipc_file_receiver_index);
    lua_setfield(L, -2, "__index");
    
    lua_pushcfunction(L, Lua_ipc_file_receiver_newindex);
    lua_setfield(L, -2, "__newindex");
  }
  lua_setmetatable(L, -2);
  return 1;
}

static shuso_ipc_receive_fd_fn lua_receive_fd_callback;

int Lua_shuso_ipc_receive_fd_start(lua_State *L) {
  int           nargs = lua_gettop(L);
  uintptr_t     fd_receiver_ref = luaL_checkinteger(L, 1);
  const char   *description = luaL_checkstring(L, 2);
  double        timeout_sec = luaL_checknumber(L, 3);
  
  lua_get_registry_table(L, "shuttlesock.ipc.fd_receiver");
  lua_pushvalue(L, 1);
  lua_rawget(L, -2);
  if(!lua_isnil(L, -1)) {
    return luaL_error(L, "fd receiver for ref %d already exists", (int )fd_receiver_ref);
  }
  lua_pop(L, 1);
  lua_pushvalue(L, 1);
  lua_push_handler_function_or_coroutine(L, nargs, NULL, true, false);
  assert(lua_type(L, -1) == LUA_TFUNCTION || lua_type(L, -1) == LUA_TTHREAD);
  lua_rawset(L, -3);
  
  bool ok = shuso_ipc_receive_fd_start(shuso_state(L), description, timeout_sec, lua_receive_fd_callback, fd_receiver_ref, NULL);
  if(!ok) {
    lua_get_registry_table(L, "shuttlesock.ipc.fd_receiver");
    lua_pushvalue(L, 1);
    lua_pushnil(L);
    lua_rawset(L, -3);
    return luaS_shuso_error(L);
  }
  
  lua_pushboolean(L, 1);
  return 1;
}

static void lua_receive_fd_callback(shuso_t *S, bool ok, uintptr_t ref, int fd, void *received_pd, void *pd) {
  
}

int Lua_shuso_ipc_receive_fd_finish(lua_State *L) {
  uintptr_t     fd_receiver_ref = luaL_checkinteger(L, 1);
  lua_get_registry_table(L, "shuttlesock.ipc.fd_receiver");
  lua_pushvalue(L, 1);
  lua_rawget(L, -2);
  if(lua_isnil(L, -1)) {
    return luaL_error(L, "fd receiver for ref %d does not exist", (int )fd_receiver_ref);
  }
  lua_pop(L, 1);
  //unreference handler
  lua_pushvalue(L, 1);
  lua_pushnil(L);
  lua_rawset(L, -3);
  
  if(!shuso_ipc_receive_fd_finish(shuso_state(L), fd_receiver_ref)) {
    return lua_push_nil_error(L);
  }
  lua_pushboolean(L, 1);
  return 1;
}


static int hostinfo_set_addr(lua_State *L, int tbl_index, const char *field_name, shuso_hostinfo_t *host, int addr_family) {
  lua_getfield(L, tbl_index, field_name);
  const char *addr = lua_tostring(L, -1);
  if(!addr) {
    return luaL_error(L, "missing %s", field_name);
  }
  if(addr_family == AF_UNIX) {
    host->path = (char *)addr;
  }
  else {
    int rc;
    if(addr_family == AF_INET) {
      rc = inet_pton(addr_family, addr, &host->addr);
    }
#ifdef SHUTTLESOCK_HAVE_IPV6
    else if(addr_family == AF_INET6) {
      rc = inet_pton(addr_family, addr, &host->addr);
    }
#endif
    else {
      return luaL_error(L, "invalid address family code %d", addr_family);
    }
    if(rc == 0) {
      return luaL_error(L, "invalid address %s", addr);
    }
    else if(rc == -1) {
      return luaL_error(L, "failed to parse address %s: %s", addr, strerror(errno));
    }
  }
  host->addr_family = addr_family;
  return 1;
}

static int shuso_lua_handle_sockopt(lua_State *L, int k, int v, shuso_sockopts_t *sockopts) {
  bool is_flag = false;
  const char *strname;
  if(lua_type(L, k) == LUA_TNUMBER && lua_type(L, v) == LUA_TSTRING) {
    strname = lua_tostring(L, v);
    is_flag = true;
  }
  else if(lua_type(L, k) == LUA_TSTRING) {
    strname = lua_tostring(L, k);
  }
  else {
    return luaL_error(L, "invalid sockopts table key");
  }
  assert(strname != NULL);
  
  shuso_system_sockopts_t *found = NULL;
  for(shuso_system_sockopts_t *known = &shuso_system_sockopts[0]; known->str != NULL; known++) {
    if(strcmp(strname, known->str) == 0) {
      found = known;
      break;
    }
  }
  if(!found) {
    return luaL_error(L, "invalid sockopt %s", strname);
  }
  
  if(is_flag) {
    if(found->value_type != SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG) {
      return luaL_error(L, "sockopt %s is not a flag", strname);
    }
    sockopts->array[sockopts->count++] = (shuso_sockopt_t ) {
      .level = found->level,
      .name = found->name,
      .value.flag = 1
    };
  }
  else {
    sockopts->array[sockopts->count++] = (shuso_sockopt_t ) {
      .level = found->level,
      .name = found->name
    };
    switch(found->value_type) {
      case SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_INT:
        sockopts->array[sockopts->count].value.integer = luaL_checkinteger(L, v);
        break;
      case SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG:
        sockopts->array[sockopts->count].value.integer = lua_toboolean(L, v);
        break;
      case SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_TIMEVAL:
        if(lua_type(L, v) != LUA_TNUMBER) {
          return luaL_error(L, "invalid timeval, must be a floating-point number");
        }
        else {
          double          time = lua_tonumber(L, v);
          struct timeval  tv;
          tv.tv_sec = (time_t )time;
          tv.tv_usec = (int )((time - (double )tv.tv_sec) * 1000.0);
          sockopts->array[sockopts->count].value.timeval = tv;
        }
        break;
      case SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_LINGER:
        if(lua_type(L, v) == LUA_TNUMBER) {
          sockopts->array[sockopts->count].value.linger.l_onoff = 1;
          sockopts->array[sockopts->count].value.linger.l_linger = lua_tointeger(L, v);
        }
        else if(lua_type(L, v) == LUA_TTABLE) {
          lua_getfield(L, v, "onoff");
          sockopts->array[sockopts->count].value.linger.l_onoff = lua_toboolean(L, -1);
          lua_pop(L, 1);
          
          lua_getfield(L, v, "linger");
          if(lua_type(L, -1) != LUA_TNUMBER) {
            return luaL_error(L, "invalid \"linger\" field in table, must be an integer");
          }
          sockopts->array[sockopts->count].value.linger.l_onoff = lua_tointeger(L, -1);
          lua_pop(L, 1);
        }
        else {  
          return luaL_error(L, "linger value must be a number or a table");
        }
        break;
      default:
        return luaL_error(L, "invalid value_type, this should never happen");
    }
  }
  return 1;
}
shuso_ipc_open_sockets_fn open_listener_sockets_callback;

#define OPEN_LISTENER_MAX_SOCKOPTS 20
static int Lua_shuso_ipc_open_listener_sockets(lua_State *L) {
  shuso_hostinfo_t host;
  luaL_checktype(L, 1, LUA_TTABLE);
  
  int nargs = lua_gettop(L);
  
  host.name = NULL;
  
  lua_getfield(L, 1, "family");
  const char *fam = luaL_checkstring(L, -1);
  
  if(strcasecmp(fam, "AF_INET") == 0 || strcasecmp(fam, "INET") == 0 || strcasecmp(fam, "ipv4") == 0) {
    hostinfo_set_addr(L, 1, "address", &host, AF_INET);
  }
  else if(strcasecmp(fam, "AF_INET6") == 0 || strcasecmp(fam, "INET6") == 0 || strcasecmp(fam, "ipv6") == 0) {
#ifndef SHUTTLESOCK_HAVE_IPV6
    return luaL_argerror(L, 1, "can't use IPv6 address, this system isn't built with IPv6 support");
#else
    hostinfo_set_addr(L, 1, "address", &host, AF_INET6);
#endif
  }
  else if(strcasecmp(fam, "AF_UNIX") == 0 || strcasecmp(fam, "UNIX")) {
    hostinfo_set_addr(L, 1, "path", &host, AF_UNIX);
  }
  else {
    return luaL_argerror(L, 1, "invalid address family");
  }
  
  lua_getfield(L, 1, "port");
  if(!lua_isinteger(L, -1)) {
    return luaL_argerror(L, 1, "port is not an integer or nil");
  }
  host.port = lua_tointeger(L, -1);
    
  lua_getfield(L, 1, "protocol");
  if(lua_isnil(L, -1)) {
    //no "protocol" field, default to TCP
    host.udp = 0;
  }
  else {
    if(!lua_isstring(L, -1)) {
      return luaL_argerror(L, 1, "protocol field is not a string or nil");
    }
    const char *protocol = lua_tostring(L, -1);
    
    if(strcasecmp(protocol, "tcp") == 0) {
      host.udp = 0;
    }
    else if(strcasecmp(protocol, "udp") == 0) {
      host.udp = 1;
    }
    else {
      return luaL_argerror(L, 1, "invalid protocol, must be \"tcp\" or \"udp\"");
    }
  }
  
  shuso_sockopt_t  sockopt[OPEN_LISTENER_MAX_SOCKOPTS];
  shuso_sockopts_t sockopts = {
    .count = 0,
    .array = sockopt
  };
  
  if(nargs > 1 && lua_istable(L, 2)) {
    lua_pushnil(L);
    for(int n = 0; lua_next(L, 2); n++) {
      if(n >= OPEN_LISTENER_MAX_SOCKOPTS) {
        return luaL_argerror(L, 2, "too many socket options");
      }
      shuso_lua_handle_sockopt(L, -2, -1, &sockopts);  
    }
  }
  else {
    sockopts.count = 1;
    sockopt[0] = (shuso_sockopt_t ){
      .level = SOL_SOCKET,
      .name = SO_REUSEPORT,
      .value.integer = 1
    };
  }
  
  
  //TODO: finish it
  
  
  
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


//lua modules
static int luaS_find_module_table(lua_State *L, const char *name) {
  lua_getglobal(L, "require");
  lua_pushliteral(L, "shuttlesock.core.module");
  lua_call(L, 1, 1);
  lua_getfield(L, -1, "find");
  lua_remove(L, -2);
  lua_pushstring(L, name);
  lua_call(L, 1, 1);
  return 1;
}

static bool lua_module_initialize_config(shuso_t *S, shuso_module_t *module, shuso_setting_block_t *block) {
  return true;
}

static void lua_module_event_listener(shuso_t *S, shuso_event_state_t *evs, intptr_t code, void *data, void *pd) {
  lua_State *L = S->lua.state;
  if(evs->data_type) {
    const shuso_event_data_type_map_t     *map;
    luaS_push_lua_module_field(L, "shuttlesock.core.module_event", "data_type_map");
    lua_pushliteral(L, "lua");
    lua_pushstring(L, evs->data_type);
    if(!luaS_function_call_result_ok(L, 2, true)) {
      shuso_set_error(S, "failed to map data type %s for event %s to Lua: %s", evs->data_type, evs->name, shuso_last_error(S));
      return;
    }
    map = lua_topointer(L, -1);
    lua_pop(L, 1);
    assert(map);
    if(!map->wrap(S, data)) {
      shuso_set_error(S, "failed to map data type %s for event %s to Lua", evs->data_type, evs->name);
      return;
    } 
  }
  else {
    lua_pushnil(L);
  }
  
  luaS_push_lua_module_field(L, "shuttlesock.module", "receive_event");
  
  lua_pushstring(L, evs->module->name);
  lua_pushstring(L, evs->name);
  
  lua_pushnil(L); //TODO: push module
  
  lua_pushvalue(L, -5);
  lua_remove(L, -6);
  
  luaS_function_call_result_ok(L, 4, false);
  return;
}

static bool lua_module_initialize(shuso_t *S, shuso_module_t *module) {
  lua_State *L = S->lua.state;
  luaS_find_module_table(L, module->name);
  
  lua_getfield(L, -1, "events");
  lua_getfield(L, -1, "publish");
  
  int npub = luaS_table_count(L, -1);
  if(npub > 0) {
    shuso_module_event_t *events = shuso_stalloc(&S->stalloc, sizeof(*events) * npub);
    shuso_event_init_t *events_init = malloc(sizeof(*events_init) * (npub + 1));
    if(events == NULL || events_init == NULL) {
      if(events_init) free(events_init);
      return shuso_set_error(S, "failed to allocate lua module published events array");
    }
    lua_pushnil(L);
    for(int i=0; lua_next(L, -2) != 0; i++) {
      assert(i<npub);
      lua_pop(L, 1);
      events_init[i]=(shuso_event_init_t ){
        .name = lua_tostring(L, -1),
        .event = &events[i]
      };
      
    }
    events_init[npub]=(shuso_event_init_t ){.name = NULL, .event = NULL};
    if(!shuso_events_initialize(S, module, events, events_init)) {
      free(events_init);
      return false;
    }
    free(events_init);
  }
  lua_pop(L, 1);
  lua_getfield(L, -1, "subscribe");
  lua_pushnil(L);
  while(lua_next(L, -2)) {
    shuso_event_listen(S, lua_tostring(L, -2), lua_module_event_listener, NULL);
    lua_pop(L, 1);
  }
  lua_pop(L, 2);
  
  bool ok = true;
  
  luaS_push_lua_module_field(L, "shuttlesock.module", "find");
  lua_pushstring(L, module->name);
  if (!luaS_function_call_result_ok(L, 1, true)) {
    lua_pop(L, 1);
    return shuso_set_error(S, "failed to find Lua shuttlesock module '%s'", module->name);
  }
  assert(lua_istable(L, -1));
  lua_getfield(L, -1, "initialize");
  if(lua_isfunction(L, -1)) {
    lua_pushvalue(L, -2);
    ok = luaS_function_pcall_result_ok(L, 1, false);
  }
  lua_pop(L, 2);
  
  return ok;
}



static int Lua_shuso_add_module(lua_State *L) {
  shuso_t *S = shuso_state(L);
  if(!(shuso_runstate_check(S, SHUSO_STATE_CONFIGURING, "add module"))) {
    lua_pushnil(L);
    lua_pushstring(L, shuso_last_error(S));
    return 2;
  }
  luaL_checktype(L, 1, LUA_TTABLE);
  shuso_module_t *m = shuso_stalloc(&S->stalloc, sizeof(*m));
  if(m == NULL) {
    lua_pushnil(L);
    lua_pushliteral(L, "failed to allocate lua module struct");
    return 2;
  }
  memset(m, '\0', sizeof(*m));
  //shuso_lua_module_data_t *d = shuso_stalloc(&S->stalloc, sizeof(*d));
  //m->privdata = d;
  
  lua_getfield(L, 1, "name");
  m->name = lua_tostring(L, -1);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "version");
  m->version = lua_tostring(L, -1);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "parent_modules");
  m->parent_modules = lua_tostring(L, -1);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "subscribe");
  m->subscribe = lua_tostring(L, -1);
  lua_pop(L, 1);
  
  lua_getfield(L, 1, "publish");
  m->publish = lua_tostring(L, -1);
  lua_pop(L, 1);
  
  m->initialize_config = lua_module_initialize_config;
  m->initialize = lua_module_initialize;
  
  if(!shuso_add_module(S, m)) {
    lua_pushnil(L);
    lua_pushfstring(L, "failed to add module: %s", shuso_last_error(S));
    return 2;
  }
  
  lua_pop(L, 1);
  lua_pushboolean(L, 1);
  return 1;
}

static int Lua_shuso_module_name(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  const shuso_module_t *module = lua_topointer(L, 1);
  lua_pushstring(L, module->name);
  return 1;
}
static int Lua_shuso_module_pointer(lua_State *L) {
  luaL_checkstring(L, 1);
  const shuso_module_t *module = shuso_get_module(shuso_state(L), lua_tostring(L, 1));
  lua_pushstring(L, module->name);
  return 1;
}

static int Lua_shuso_module_version(lua_State *L) {
  int lt = lua_type(L, 1);
  const shuso_module_t *module;
  if(lt == LUA_TLIGHTUSERDATA) {
    module = lua_topointer(L, 1);
  }
  else if(lt == LUA_TSTRING) {
    module = shuso_get_module(shuso_state(L), lua_tostring(L, 1));
    if(!module) {
      lua_pushnil(L);
      lua_pushliteral(L, "no such module");
      return 2;
    }
  }
  else {
    lua_pushnil(L);
    lua_pushliteral(L, "module_version argument must be a light userdata or string");
    return 2;
  }
  lua_pushstring(L, module->version);
  return 1;
}

static void lua_module_gxcopy(shuso_t *S, shuso_event_state_t *es, intptr_t code, void *data, void *pd) {
  shuso_t *Sm = data;
  lua_State *L = S->lua.state;
  lua_State *Lm = Sm->lua.state;
  
  //copy over all required modules that have a metatable and __gxcopy
  lua_getglobal(Lm, "package");
  lua_getfield(Lm, -1, "loaded");
  lua_remove(Lm, -2);
  lua_pushnil(Lm);  /* first key */
  while(lua_next(Lm, -2) != 0) {
    if(!lua_getmetatable(Lm, -1)) {
      lua_pop(Lm, 1);
      continue;
    }
    lua_getfield(Lm, -1, "__gxcopy_save_module_state");
    if(lua_isnil(Lm, -1)) {
      lua_pop(Lm, 3);
      continue;
    }
    lua_pop(Lm, 3);
    
    luaS_gxcopy_module_state(Lm, L, lua_tostring(Lm, -1));
  }
  lua_pop(Lm, 1);
}

typedef struct {
  int           placeholder;
} lua_bridge_module_ctx_t;

/*
static void *lua_shared_allocator(void *ud, void *ptr, size_t osize, size_t nsize) {
  shuso_shared_slab_t *shm = ud;
  if (nsize == 0) {
    shuso_shared_slab_free(shm, ptr);
    return NULL;
  }
  else if(ptr == NULL || osize == 0) {
    return shuso_shared_slab_alloc_locked(shm, nsize);
  }
  else {
    void *newptr = shuso_shared_slab_alloc_locked(shm, nsize);
    if(newptr == NULL) {
      return NULL;
    }
    memcpy(newptr, ptr, nsize < osize ? nsize : osize);
    shuso_shared_slab_free_locked(shm, ptr);
    return newptr;
  }
}
*/

static bool lua_bridge_module_initialize(shuso_t *S, shuso_module_t *self) {
  shuso_event_listen(S, "core:worker.start.before.lua_gxcopy", lua_module_gxcopy, self);
  
  lua_bridge_module_ctx_t *ctx = shuso_stalloc(&S->stalloc, sizeof(*ctx));
  
  shuso_set_core_context(S, self, ctx);
  
  return true;
}

static int Lua_shuso_block_parent_setting_pointer(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  const shuso_setting_block_t *block = lua_topointer(L, 1);
  lua_pushlightuserdata(L, block->setting);
  return 1;
}

static int Lua_shuso_setting_block_pointer(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  const shuso_setting_t *setting = lua_topointer(L, 1);
  if(setting->block) {
    lua_pushnil(L);
  }
  else {
    lua_pushlightuserdata(L, setting->block);
  }
  return 1;
}

static const shuso_setting_values_t *setting_values_type(lua_State *L, const shuso_setting_t     *setting, int nindex) {
  lua_pushliteral(L, "merged");
  if(nindex == 0 || lua_compare(L, nindex, -1, LUA_OPEQ)) {
    lua_pop(L, 1);
    return setting->values.merged;
  }
  lua_pop(L, 1);
  
  lua_pushliteral(L, "local");
  if(lua_compare(L, nindex, -1, LUA_OPEQ)) {
    lua_pop(L, 1);
    return setting->values.local;
  }
  lua_pop(L, 1);
  
  lua_pushliteral(L, "inherited");
  if(lua_compare(L, nindex, -1, LUA_OPEQ)) {
    lua_pop(L, 1);
    return setting->values.inherited;
  }
  lua_pop(L, 1);
  
  lua_pushliteral(L, "default");
  lua_pushliteral(L, "defaults");
  if(lua_compare(L, nindex, -1, LUA_OPEQ) || lua_compare(L, nindex, -2, LUA_OPEQ)) {
    lua_pop(L, 2);
    return setting->values.defaults;
  }
  lua_pop(L, 2);
  
  lua_getglobal(L, "tostring");
  lua_pushvalue(L, nindex);
  lua_call(L, 1, 1);
  luaL_error(L, "invalid setting value type '%d', must be 'merged', 'local', 'inherited', or 'default'", lua_tostring(L, -1));
  return NULL;
}

static int Lua_shuso_setting_values_count(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  
  const shuso_setting_t         *setting = lua_topointer(L, 1);
  const shuso_setting_values_t  *vals = NULL;
  
  vals = setting_values_type(L, setting, lua_gettop(L) < 2 ? 0 : 2);
  assert(vals != NULL);
  
  lua_pushinteger(L, vals->count);
  return 1;
}

static int Lua_shuso_setting_value(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  int n = luaL_checkinteger(L, 2);
  
  const shuso_setting_t        *setting = lua_topointer(L, 1);
  const shuso_setting_values_t *vals = NULL;
  
  vals = setting_values_type(L, setting, lua_gettop(L) < 3 ? 0 : 3);
  assert(vals != NULL);
  
  if(n < 1) {
    lua_pushnil(L);
    lua_pushfstring(L, "invalid value index %d (as in lua, the indices start at 1, not 0)", n);
    return 2;
  }
  else if(vals->count > n) {
    lua_pushnil(L);
    lua_pushfstring(L, "no value at index %d", n);
    return 2;
  }
  
  const shuso_setting_value_t *val = &vals->array[n-1];
  
  lua_createtable(L, 0, 5);
  if(val->valid.boolean) {
    lua_pushboolean(L, val->boolean);
    lua_setfield(L, -2, "boolean");
  }
  if(val->valid.integer) {
    lua_pushinteger(L, val->integer);
    lua_setfield(L, -2, "integer");
  }
  if(val->valid.number) {
    lua_pushnumber(L, val->number);
    lua_setfield(L, -2, "number");
  }
  if(val->valid.string) {
    lua_pushlstring(L, val->string, val->string_len);
    lua_setfield(L, -2, "string");
  }
  
  lua_pushlstring(L, val->raw, val->raw_len);
  lua_setfield(L, -2, "raw");
  
  return 1;
}

static int Lua_shuso_setting_name(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  const shuso_setting_t        *setting = lua_topointer(L, 1);
  lua_pushstring(L, setting->name);
  return 1;
}
static int Lua_shuso_setting_raw_name(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  const shuso_setting_t        *setting = lua_topointer(L, 1);
  lua_pushstring(L, setting->raw_name);
  return 1;
}
static int Lua_shuso_setting_module_name(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  const shuso_setting_t        *setting = lua_topointer(L, 1);
  lua_pushstring(L, setting->module);
  return 1;
}

luaL_Reg shuttlesock_core_module_methods[] = {
// creation, destruction
  {"create", Lua_shuso_create},
  {"destroy", Lua_shuso_destroy},

//configuration
  {"configure_file", Lua_shuso_configure_file},
  {"configure_string", Lua_shuso_configure_string},
  //{"configure_handlers", Lua_shuso_configure_handlers},
  //{"add_module", Lua_shuso_add_module},
  {"configure_finish", Lua_shuso_configure_finish},
  
//state 
  {"run", Lua_shuso_run},
  {"stop", Lua_shuso_stop},
  {"runstate", Lua_shuso_runstate},
  {"process_runstate", Lua_shuso_process_runstate},

  {"set_log_file", Lua_shuso_set_log_fd},
  {"set_error", Lua_shuso_set_error},

//config
  {"config_block_parent_setting_pointer", Lua_shuso_block_parent_setting_pointer},
  {"config_setting_block_pointer", Lua_shuso_setting_block_pointer},
  {"config_setting_value", Lua_shuso_setting_value},
  {"config_setting_name", Lua_shuso_setting_name},
  {"config_setting_raw_name", Lua_shuso_setting_raw_name},
  {"config_setting_module_name", Lua_shuso_setting_module_name},
  {"config_setting_values_count", Lua_shuso_setting_values_count},

//watchers
  {"new_watcher", Lua_shuso_new_watcher},
  
//shared slab
  {"shared_slab_alloc_string", Lua_shuso_shared_slab_alloc_string},
  {"shared_slab_free_string", Lua_shuso_shared_slab_free_string},

//resolver
  {"resolve", Lua_shuso_resolve_hostname},
  
//logger
  {"log", Lua_shuso_log},
  {"log_debug", Lua_shuso_log_debug},
  {"log_info", Lua_shuso_log_info},
  {"log_notice", Lua_shuso_log_notice},
  {"log_warning", Lua_shuso_log_warning},
  {"log_error", Lua_shuso_log_error},
  {"log_critical", Lua_shuso_log_critical},
  {"log_fatal", Lua_shuso_log_fatal},

//modules
  {"add_module", Lua_shuso_add_module},
  {"module_pointer", Lua_shuso_module_pointer},
  {"module_name", Lua_shuso_module_name},
  {"module_version", Lua_shuso_module_version},
  
//ipc
  {"send_file", Lua_shuso_ipc_send_fd},
  {"new_file_receiver", Lua_shuso_ipc_file_receiver_new},
  {"open_listener_sockets", Lua_shuso_ipc_open_listener_sockets},
  {"add_message_handler", Lua_shuso_ipc_add_handler},
  {"send_message", Lua_shuso_ipc_send},
  {"send_message_to_all_workers", Lua_shuso_ipc_send_workers},
  
  {NULL, NULL}
};

luaL_Reg shuttlesock_system_module_methods[] = {
  {"glob", luaS_glob},
  //{"cores_online", Lua_shuso_cores_online},
  //{"strsignal", Lua_shuso_strsignal},
  {NULL, NULL}
};

int luaS_push_core_module(lua_State *L) {
  luaL_newlib(L, shuttlesock_core_module_methods);
  return 1;
}

int luaS_push_system_module(lua_State *L) {
  luaL_newlib(L, shuttlesock_system_module_methods);
  return 1;
}

shuso_module_t shuso_lua_bridge_module = {
  .name = "lua_bridge",
  .version = "0.0.1",
  .subscribe = 
   " core:worker.start.before.lua_gxcopy",
  .initialize = lua_bridge_module_initialize
};
