#include <shuttlesock.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <unistd.h>
#include <strings.h>
#include <arpa/inet.h>
#include <ares.h>
#include <errno.h>
#include <pthread.h>
#include "api/lua_ipc.h"
#include <shuttlesock/modules/config/private.h>

typedef enum {
  LUA_EV_WATCHER_IO =       0,
  LUA_EV_WATCHER_TIMER =    1,
  LUA_EV_WATCHER_CHILD =    2,
  LUA_EV_WATCHER_SIGNAL =   3
} shuso_lua_ev_watcher_type_t;


typedef struct shuso_lua_ev_watcher_s {
  shuso_ev_any      watcher;
  struct {
    int               self;
    int               handler;
  }                 ref;
  unsigned          type:4;
  lua_State        *coroutine_thread;
} shuso_lua_ev_watcher_t;

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
static shuso_process_t *lua_shuso_checkprocnum(lua_State *L, int index);

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

static int Lua_shuso_pointer(lua_State *L) {
  shuso_t *S = shuso_state(L);
  lua_pushlightuserdata(L, S);
  return 1;
}

static int Lua_shuso_procnum_valid(lua_State *L) {
  const char *err = "invalid procnum";
  int procnum = luaL_checkinteger(L, 1);
  if(shuso_procnum_valid(shuso_state(L), procnum, &err)){
    lua_pushboolean(L, 1);
    return 1;
  }
  else {
    lua_pushboolean(L, 0);
    lua_pushstring(L, err);
    return 2;
  }
}

static int Lua_shuso_procnum(lua_State *L) {
  shuso_t *S = shuso_state(L);
  lua_pushinteger(L, S->procnum);
  return 1;
}

static int Lua_shuso_count_workers(lua_State *L) {
  shuso_t *S = shuso_state(L);
  lua_pushinteger(L, *S->common->process.workers_end - *S->common->process.workers_start);
  return 1;
}

static int Lua_shuso_procnums_active(lua_State *L) {
  shuso_t *S = shuso_state(L);
  lua_newtable(L);
  int i = 1;
  lua_pushinteger(L, SHUTTLESOCK_MASTER);
  lua_rawseti(L, -2, i++);
  
  if(*S->common->process.manager.state >= SHUSO_STATE_STARTING) {
    lua_pushinteger(L, SHUTTLESOCK_MANAGER);
    lua_rawseti(L, -2, i++);
  }
  unsigned start = *S->common->process.workers_start, end = *S->common->process.workers_end;
  
  for(unsigned w = start; w < end; w++) {
    lua_pushinteger(L, w);
    lua_rawseti(L, -2, i++);
  }
  
  return 1;
}

static int Lua_shuso_process_runstate(lua_State *L) {
  shuso_t *S = shuso_state(L);
  if(lua_gettop(L) == 0) {
    luaS_push_runstate(L, *S->process->state);
    return 1;
  }
  
  shuso_process_t *proc = lua_shuso_checkprocnum(L, 1);
  luaS_push_runstate(L, *proc->state);
  return 1;
}

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
  
  if(nargs > 1) {
    lua_getlib_field(L, "string", "format");
    lua_call(L, nargs, 1);
  }
  
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
    luaL_error(L, "no handler for watcher");
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
    rc = luaS_resume(coro, L, 1);
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
  luaL_checkstring(L, 2);
  //watcher.handler=(function)
  if(luaS_streq_literal(L, 2, "handler")) {
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
  else if(w->type == LUA_EV_WATCHER_TIMER) {
    //watcher.after=(float)
    if(luaS_streq_literal(L, 2, "after")) {
      union { //stop type-punning warning from complaining
        ev_timer *ev;
        ev_watcher_time *watcher;
      } ww = { .ev = &w->watcher.timer.ev }; 
      ww.watcher->at = luaL_checknumber(L, 3);
    }
    //watcher.repeat=(float)
    else if(luaS_streq_literal(L, 2, "repeat")) {
      w->watcher.timer.ev.repeat = luaL_checknumber(L, 3);
    }
  }
  else {
    return luaL_error(L, "don't know how to set shuttlesock %s watcher field \"%s\"", watchertype_str(w->type), lua_tostring(L, 2));
  }
  return 0;
}

static int Lua_watcher_index(lua_State *L) {
  shuso_lua_ev_watcher_t *w = luaL_checkudata(L, 1, "shuttlesock.watcher");
  if(luaS_streq_literal(L, 2, "set")) {
    lua_pushcfunction(L, Lua_watcher_set);
    return 1;
  }
  else if(luaS_streq_literal(L, 2, "start")) {
    lua_pushcfunction(L, Lua_watcher_start);
    return 1;
  }
  else if(luaS_streq_literal(L, 2, "yield")) {
    lua_pushcfunction(L, Lua_watcher_yield);
    return 1;
  }
  else if(luaS_streq_literal(L, 2, "stop")) {
    lua_pushcfunction(L, Lua_watcher_stop);
    return 1;
  }
  else if(luaS_streq_literal(L, 2, "active")) {
    lua_pushboolean(L, ev_is_active(&w->watcher.watcher) || ev_is_pending(&w->watcher.watcher));
    return 1;
  }
  else if(luaS_streq_literal(L, 2, "pending")) {
    lua_pushboolean(L, ev_is_pending(&w->watcher.watcher));
    return 1;
  }
  else if(luaS_streq_literal(L, 2, "handler")) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, w->ref.handler);
    //TODO: check that LUA_REGISTRYINDEX[LUA_NOREF] == nil
    return 1;
  }
  else if(luaS_streq_literal(L, 2, "type")) {
    lua_pushstring(L, watchertype_str(w->type));
    return 1;
  }
  
  else if(w->type == LUA_EV_WATCHER_IO){
    if(luaS_streq_literal(L, 2, "fd")) {
      lua_pushinteger(L, w->watcher.io.ev.fd);
    }
    else if(luaS_streq_literal(L, 2, "events")) {
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
    if(luaS_streq_literal(L, 2, "repeat")) {
      lua_pushnumber(L, w->watcher.timer.ev.repeat);
    }
    else if(luaS_streq_literal(L, 2, "after")) {
      union { //stop type-punning warning from complaining
        ev_timer *ev;
        ev_watcher_time *watcher;
      } ww = { .ev = &w->watcher.timer.ev }; 
      lua_pushnumber(L, ww.watcher->at);
    }
    return 1;
  }
  
  else if(w->type == LUA_EV_WATCHER_SIGNAL) {
    if(luaS_streq_literal(L, 2, "signum")) {
      lua_pushinteger(L, w->watcher.signal.ev.signum);
    }
    return 1;
  }
  
  else if(w->type == LUA_EV_WATCHER_CHILD) {
    if(luaS_streq_literal(L, 2, "pid")) {
      lua_pushinteger(L, w->watcher.child.ev.pid);
    }
    else if(luaS_streq_literal(L, 2, "rpid")) {
      lua_pushinteger(L, w->watcher.child.ev.rpid);
    }
    else if(luaS_streq_literal(L, 2, "rstatus")) {
      lua_pushinteger(L, w->watcher.child.ev.rstatus);
    }
    return 1;
  }
  
  return luaL_error(L, "unknown field \"%s\" for shuttlesock %s watcher", lua_tostring(L, 2), watchertype_str(w->type));
}

int Lua_shuso_new_watcher(lua_State *L) {
  shuso_lua_ev_watcher_type_t  wtype;
  luaL_checkstring(L, 1);
  
  if(luaS_streq_literal(L, 1, "io")) {
    wtype = LUA_EV_WATCHER_IO;
  }
  else if(luaS_streq_literal(L, 1, "timer")) {
    wtype = LUA_EV_WATCHER_TIMER;
  }
  else if(luaS_streq_literal(L, 1, "child")) {
    wtype = LUA_EV_WATCHER_CHILD;
  }
  else if(luaS_streq_literal(L, 1, "signal")) {
    wtype = LUA_EV_WATCHER_SIGNAL;
  }
  else {
    return luaL_error(L, "invalid watcher type \"%s\"", lua_tostring(L, 1));
  }
  
  shuso_lua_ev_watcher_t *watcher;
  if((watcher = lua_newuserdata(L, sizeof(*watcher))) == NULL) {
    return luaL_error(L, "unable to allocate memory for new watcher");
  }
  
  memset(&watcher->watcher, 0, sizeof(watcher->watcher));
  watcher->type = wtype;
  watcher->ref.handler = LUA_NOREF;
  watcher->ref.self = LUA_NOREF;
  watcher->coroutine_thread = NULL;
  
  if(luaL_newmetatable(L, "shuttlesock.watcher")) {
    luaL_setfuncs(L, (luaL_Reg[]) {
      {"__gc", Lua_watcher_gc},
      {"__index", Lua_watcher_index},
      {"__newindex", Lua_watcher_newindex},
      {NULL, NULL}
    }, 0);
  }
  lua_setmetatable(L, -2);
  
  ev_init(&watcher->watcher.watcher, watcher_callback);
  
  
  if(lua_gettop(L) > 1) {
    lua_pushcfunction(L, Lua_watcher_set);
    lua_replace(L, 1);
    lua_insert(L, 2);
    luaS_pcall(L, lua_gettop(L) - 1, 1);
  }
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
  shuso_t       *S = shuso_state(L);
  luaL_checkstring(L, 1);
  logfunc(S, "%s", lua_tostring(L, 1));
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
  else if(type != LUA_TNUMBER) {
    luaL_error(L, "procnum must be a number or string");
    return NULL;
  }
  else {
    lua_pushcfunction(L, Lua_shuso_procnum_valid);
    
    lua_pushvalue(L, index);
    lua_call(L, 1, 2);
    if(!lua_toboolean(L, -2)) {
      lua_error(L);
      return NULL;
    }
    lua_pop(L, 2);
    proc = &S->common->process.worker[lua_tointeger(L, index)];
  }
  return proc;
}

static int Lua_shuso_ipc_send_fd(lua_State *L) {
  shuso_process_t *proc = lua_shuso_checkprocnum(L, 1);
  int              fd = luaL_checkinteger(L, 2);
  uintptr_t        ref = luaL_checkinteger(L, 3);
  
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

/*
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
  int nsocks = luaL_checkinteger(L, 2);
  if(!lua_isyieldable(L)) {
    return luaL_error(L, "must be called from yieldable coroutine");
  }
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
  
  if(nargs > 2 && lua_istable(L, 2)) {
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
    sockopt[0] = (shuso_sockopt_t ) {
      .level = SOL_SOCKET,
      .name = SO_REUSEPORT,
      .value.integer = 1
    };
  }
  shuso_t *S = shuso_state(L);
  
  bool ok = shuso_ipc_command_open_listener_sockets(S, &host, nsocks, &sockopts, open_listener_sockets_callback, L);
  if(!ok) {
    lua_pushnil(L);
    lua_pushfstring(L, "failed to open listener sockets: %s", shuso_last_error(S));
    return 2;
  }
  lua_pushboolean(L, 1);
  lua_yield(L, 1);
  return 1;
}

static void open_listener_sockets_callback(shuso_t *S, shuso_status_t status, shuso_hostinfo_t *h, int *sockets, int socket_count, void *pd) {
  lua_State *thread = pd;
  
}
*/

//lua modules
/*
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
*/

static bool lua_module_initialize_config(shuso_t *S, shuso_module_t *module, shuso_setting_block_t *block) {
  lua_State *L = S->lua.state;
  int        top = lua_gettop(L);
  
  luaS_push_lua_module_field(L, "shuttlesock.module", "find");
  lua_pushstring(L, module->name);
  if (!luaS_function_call_result_ok(L, 1, true)) {
    lua_settop(L, top);
    return shuso_set_error(S, "failed to find Lua shuttlesock module '%s'", module->name);
  }
  
  lua_getfield(L, -1, "initialize_config");
  if(!lua_isfunction(L, -1)) {
    lua_settop(L, top);
    return true;
  }
  lua_pushvalue(L, -2);
  lua_remove(L, -3);
  
  luaS_push_lua_module_field(L, "shuttlesock.config", "block");
  lua_pushlightuserdata(L, block);
  if (!luaS_function_call_result_ok(L, 1, true)) {
    lua_settop(L, top);
    return shuso_set_error(S, "failed to wrap config block for Lua shuttlesock module '%s'", module->name);
  }
  
  if(!luaS_call_noerror(L, 2, LUA_MULTRET)) {
    shuso_set_error(S, "failed run initialize_config for Lua shuttlesock module '%s': %s", module->name, lua_tostring(L, -1));
    lua_settop(L, top);
    return false;
  }
  
  return true;
}

static bool luaS_push_wrapped_event_data(lua_State *L, const char *type, void *data) {
  if(type == NULL || data == NULL) {
    lua_pushnil(L);
    return true;
  }
  
  lua_pushstring(L, type);
  if(luaS_streq_literal(L, -1, "string")) {
    lua_pop(L, 1);
    lua_pushstring(L, (char *)data);
    return true;
  }
  else if(luaS_streq_literal(L, -1, "float")) {
    lua_pop(L, 1);
    lua_pushnumber(L, *(double *)data);
    return true;
  }
  else if(luaS_streq_literal(L, -1, "integer")) {
    lua_pop(L, 1);
    lua_pushinteger(L, *(int *)data);
    return true;
  }
  else {
    shuso_set_error(shuso_state(L), "don't know how to wrap event data type '%s'", type);
    lua_pop(L, 1);
    lua_pushnil(L);
    return false;
  }
}

static bool luaS_wrap_event_data_cleanup(lua_State *L, const char *type, void *data) {
  return true;
}

static lua_reference_t luaS_unwrap_event_data(lua_State *L, const char *type, int narg, void **ret) {
  if(type  == NULL) {
    *ret = NULL;
    return LUA_NOREF;
  }
  
  lua_pushstring(L, type);
  if(luaS_streq_literal(L, -1, "string")) {
    lua_pop(L, 1);
    *(const char **)ret = lua_tostring(L, narg);
    lua_pushvalue(L, narg);
    return luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else if(luaS_streq_literal(L, -1, "float")) {
    lua_pop(L, 1);
    double *dubs = lua_newuserdata(L, sizeof(double));
    *dubs = lua_tonumber(L, narg);
    *ret = dubs;
    return luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else if(luaS_streq_literal(L, -1, "integer")) {
    int *integer = lua_newuserdata(L, sizeof(int));
    *integer = lua_tointeger(L, narg);
    *ret = integer;
    return luaL_ref(L, LUA_REGISTRYINDEX);
  }
  else {
    *ret = NULL;
    shuso_set_error(shuso_state(L), "don't know how to unwrap event data type '%s'", type);
    return LUA_NOREF;
  }
}

static void luaS_unwrap_event_data_cleanup(lua_State *L, const char *datatype, lua_reference_t ref, void *data) {
  if(ref == LUA_REFNIL || ref == LUA_NOREF) {
    //nothing to clean up
    return;
  }
  luaL_unref(L, LUA_REGISTRYINDEX, ref);
  return;
}

static void lua_module_event_listener(shuso_t *S, shuso_event_state_t *evs, intptr_t code, void *data, void *pd) {
  lua_State *L = S->lua.state;
  int top = lua_gettop(L);
  luaS_push_lua_module_field(L, "shuttlesock.module", "event_subscribers");
  intptr_t fn_index = (intptr_t )pd;
  lua_rawgeti(L, -1, fn_index);
  assert(lua_isfunction(L, -1));
  lua_remove(L, -2);
  lua_pushstring(L, evs->publisher->name);
  lua_pushstring(L, evs->module->name);
  lua_pushstring(L, evs->name);
  lua_pushinteger(L, code);
  
  luaS_push_wrapped_event_data(L, evs->data_type, data);
  
  lua_pushlightuserdata(L, evs);

  luaS_function_call_result_ok(L, 6, false);
  
  luaS_wrap_event_data_cleanup(L, evs->data_type, data);
  
  assert(lua_gettop(L) == top);
  return;
}

typedef struct {
  shuso_module_event_t   *events;
  int                     events_count;
} lua_module_core_ctx_t;

static bool lua_module_initialize(shuso_t *S, shuso_module_t *module) {
  lua_State *L = S->lua.state;
  int top = lua_gettop(L);
  
  lua_module_core_ctx_t *ctx = shuso_stalloc(&S->stalloc, sizeof(*ctx));
  shuso_set_core_context(S, module, ctx);
  ctx->events = NULL;
  
  luaS_push_lua_module_field(L, "shuttlesock.module", "find");
  lua_pushstring(L, module->name);
  if (!luaS_function_call_result_ok(L, 1, true)) {
    lua_settop(L, top);
    return shuso_set_error(S, "failed to find Lua shuttlesock module '%s'", module->name);
  }
  assert(lua_istable(L, -1));
  
  luaS_push_lua_module_field(L, "shuttlesock.module", "module_publish_events");
  lua_getfield(L, -1, module->name);
  lua_remove(L, -2);
  int npub;
  if(lua_istable(L, -1) && (npub = luaS_table_count(L, -1)) > 0) {
    shuso_module_event_t *events = shuso_stalloc(&S->stalloc, sizeof(*events) * npub);
    if(events == NULL) {
      lua_settop(L, top);
      return shuso_set_error(S, "failed to allocate lua module published events array");
    }
    
    ctx->events = events;
    ctx->events_count = npub;
    lua_pushnil(L);
    int i = 0;
    while(lua_next(L, -2)) {
      assert(lua_istable(L, -1));
      
      shuso_event_init_t evinit = {0};

      evinit.name = lua_tostring(L, -2);
      
      lua_getfield(L, -1, "cancelable");
      evinit.cancelable = lua_toboolean(L, -1);
      lua_pop(L, 1);
      
      lua_getfield(L, -1, "cancellable"); //throw the brits a bone
      evinit.cancelable = evinit.cancelable || lua_toboolean(L, -1);
      lua_pop(L, 1);
      
      lua_getfield(L, -1, "data_type");
      evinit.data_type = lua_tostring(L, -1);
      lua_pop(L, 1);
      
      if(!shuso_event_initialize(S, module, &events[i], &evinit)) {
        lua_pushboolean(L, 1);
        lua_setfield(L, -2, "initialization_failed");
      }
      else {
        lua_pushinteger(L, i);
        lua_setfield(L, -2, "index");
      }
      
      i++;
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  
  luaS_push_lua_module_field(L, "shuttlesock.module", "module_subscribers");
  lua_pushvalue(L, -2);
  lua_gettable(L, -2);
  lua_remove(L, -2);
  if(lua_istable(L, -1)) {
    lua_pushnil(L);
    while(lua_next(L, -2)) {
      const char *event_name = lua_tostring(L, -2);
      lua_pushnil(L);
      while(lua_next(L, -2)) {
        
        lua_getfield(L, -1, "optional_event_name");
        if(lua_isstring(L, -1)) {
          event_name = lua_tostring(L, -1);
        }
        lua_pop(L, 1);
        
        lua_getfield(L, -1, "priority");
        int priority = lua_tointeger(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, -1, "index");
        intptr_t fn_index = lua_tointeger(L, -1);
        lua_pop(L, 1);
        shuso_event_listen_with_priority(S, event_name, lua_module_event_listener, (void *)fn_index, priority);
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  
  assert(top + 1 == lua_gettop(L));
  
  int stacksize_before = lua_gettop(L);

  lua_getfield(L, -1, "initialize");
  if(lua_isfunction(L, -1)) {
    lua_pushvalue(L, -2);
    
    if(!luaS_pcall(L, 1, LUA_MULTRET)) {
      lua_settop(L, top);
      return false;
    }
    int nret = lua_gettop(L) - stacksize_before;
    if(nret > 0 && !lua_toboolean(L, stacksize_before+1)) {
      const char *err = nret > 1 ? lua_tostring(L, stacksize_before+2) : "(no error message)";
      shuso_set_error(S, "%s", err);
      lua_settop(L, top);
      return false;
    }
    lua_pop(L, nret);
  }
  
  luaS_push_lua_module_field(L, "shuttlesock.core", "gxcopy_check");
  lua_pushvalue(L, -2);
  lua_pushfstring(L, "failed to initialize module %s", module->name);
  lua_call(L, 2, 2);
  if(!lua_toboolean(L, -2)) {
    const char *err = lua_tostring(L, -1);
    shuso_set_error(S, "%s", err);
    lua_settop(L, top);
    return false;
  }
  lua_settop(L, top);
  return true;
}

static int Lua_shuso_add_module(lua_State *L) {
  shuso_t *S = shuso_state(L);
  if(!(shuso_runstate_check(S, SHUSO_STATE_CONFIGURING, "add module"))) {
    return luaL_error(L, shuso_last_error(S));
  }
  luaL_checktype(L, 1, LUA_TTABLE);
  shuso_module_t *m = shuso_stalloc(&S->stalloc, sizeof(*m));
  if(m == NULL) {
    return luaL_error(L, "failed to allocate lua module struct");
  }
  memset(m, '\0', sizeof(*m));
  //shuso_lua_module_data_t *d = shuso_stalloc(&S->stalloc, sizeof(*d));
  //m->privdata = d;
  
  //don't need to check these fields, their values will be checked by the C module adder
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
  
  lua_getfield(L, 1, "settings");
  
  //settings need to be generated, thus also checked
  if(!lua_isnil(L, -1) && !lua_istable(L, -1)) {
    return luaL_error(L, "settings field is not a nil or table", m->name);
  }
  if(lua_istable(L, -1)) {
    int count = luaL_len(L, -1);
    shuso_module_setting_t *settings = shuso_stalloc(&S->stalloc, sizeof(*settings) * (count+1));
    if(!settings) {
      return luaL_error(L, "not enough memory for settings");
    }
    for(int i = 0; i < count; i++) {
      lua_geti(L, -1, i+1);
      if(!lua_istable(L, -1)) {
        return luaL_error(L, "settings value is not a table");
      }
      
      lua_getfield(L, -1, "name");
      if(!lua_isstring(L, -1)) {
        return luaL_error(L, "setting.name is not a string");
      }
      settings[i].name = lua_tostring(L, -1);
      lua_pop(L, 1);
      
      lua_getfield(L, -1, "aliases");
      if(lua_istable(L, -1)) {
        luaS_table_concat(L, " ");
        settings[i].aliases = shuso_stalloc(&S->stalloc, luaL_len(L, -1)+1);
        strcpy((char *)settings[i].aliases, lua_tostring(L, -1));
      }
      else if(lua_isstring(L, -1)) {
        settings[i].aliases = lua_tostring(L, -1);
      }
      else if(!lua_isnil(L, -1)) {
        return luaL_error(L, "setting.aliases is not a table, string, or nil");
      }
      lua_pop(L, 1);
      
      lua_getfield(L, -1, "path");
      if(!lua_isstring(L, -1)) {
        return luaL_error(L, "setting.path is not a string");
      }
      settings[i].path = lua_tostring(L, -1);
      lua_pop(L, 1);
      
      lua_getfield(L, -1, "description");
      settings[i].description = lua_tostring(L, -1);
      lua_pop(L, 1);
      
      lua_getfield(L, -1, "nargs");
      settings[i].nargs = lua_tostring(L, -1);
      lua_pop(L, 1);
      
      lua_getfield(L, -1, "default_value");
      if(!lua_isstring(L, -1) && !lua_isnil(L, -1)) {
        return luaL_error(L, "setting.default_value is not a nil or string");
      }
      settings[i].default_value = lua_tostring(L, -1);
      lua_pop(L, 1);
      
      lua_getfield(L, -1, "default_value");
      if(lua_isstring(L, -1) && (luaS_streq(L, -1, "maybe") || luaS_streq(L, -1, "optional"))) {
        settings[i].block = SHUSO_SETTING_BLOCK_OPTIONAL;
      }
      else {
        settings[i].block = lua_toboolean(L, -1);
      }
      lua_pop(L, 1);
      
      lua_pop(L, 1);
    }
    settings[count] = (shuso_module_setting_t ){.name = NULL};
    m->settings = settings;
  }
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
  lua_pushlightuserdata(L, (void *)module);
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

static int Lua_shuso_module_event_cancel(lua_State *L) {
  shuso_t *S = shuso_state(L);
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  shuso_event_state_t *evstate = (void *)lua_topointer(L, 1);
  if(shuso_event_cancel(S, evstate)) {
    lua_pushboolean(L, 1);
    return 1;
  }
  else {
    lua_pushnil(L);
    lua_pushliteral(L, "event cannot be canceled");
    return 2;
  }
}

static int Lua_shuso_module_event_pause(lua_State *L) {
  shuso_t *S = shuso_state(L);
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  shuso_event_state_t *evstate = (void *)lua_topointer(L, 1);
  
  shuso_module_paused_event_t *paused;
  
  if(lua_gettop(L) >= 2) {
    paused = luaL_checkudata(L, 2, "paused_event");
    lua_pushvalue(L, 2);
  }
  else {
    paused = lua_newuserdata(L, sizeof(*paused));
    if(!paused) {
      lua_pushnil(L);
      lua_pushliteral(L, "event could not be paused because we're out of memory");
      return 2;
    }
    luaL_newmetatable(L, "paused_event");
    lua_setmetatable(L, -2);
  }
  
  if(shuso_event_pause(S, evstate, paused)) {
    //return the userdata on the stack
    return 1;
  }
  else {
    lua_pushnil(L);
    lua_pushliteral(L, "event cannot be paused");
    return 2;
  }
}

static int Lua_shuso_module_event_resume(lua_State *L) {
  shuso_t *S = shuso_state(L);
  shuso_module_paused_event_t *paused = luaL_checkudata(L, 1, "paused_event");
  
  bool ok = shuso_event_resume(S, paused);
  lua_pushboolean(L, ok);
  return 1;
}

static int Lua_shuso_module_event_publish(lua_State *L) {
  shuso_t                 *S = shuso_state(L);
  int                      nargs = lua_gettop(L);
  const char              *modname = luaL_checkstring(L, 1);
  const char              *evname = luaL_checkstring(L, 2);
  intptr_t                 code;
  
  if(nargs < 3) {    
    code = 0;
  }
  else {
    switch(lua_type(L, 3)) {
      case LUA_TNUMBER:
        code = lua_tointeger(L, 3);
        break;
      case LUA_TBOOLEAN:
        code = lua_toboolean(L, 3);
        break;
      case LUA_TNIL:
        code = 0;
        break;
      /* could be allowed in theory, but it's probably best forbidden to train developers of non-C modules
      case LUA_TUSERDATA:
      case LUA_TLIGHTUSERDATA:
        code = lua_topointer(L, 3);
        break;
      */
      default:
        lua_pushnil(L);
        lua_pushfstring(L, "published event code cannot have type %s", luaL_typename(L, 3));
        return 2;
    }
  }
  
  shuso_module_t          *module = shuso_get_module(S, modname);
  if(!module) {
    lua_pushnil(L);
    lua_pushfstring(L, "no such module '%s'", modname);
    return 2;
  }
  lua_module_core_ctx_t   *ctx = shuso_core_context(S, module);
  assert(ctx);
  
  luaS_push_lua_module_field(L, "shuttlesock.module", "module_publish_events");
  lua_getfield(L, -1, modname);
  lua_remove(L, -2);
  if(!lua_istable(L, -1)) {
    return luaL_error(L, "couldn't find module_publish_events table");
  }
  
  lua_getfield(L, -1, evname);
  if(lua_isnil(L, -1)) {
    lua_pushnil(L);
    lua_pushfstring(L, "can't publish event '%s:%s', it's not registed as a publishable event for module %s", modname, evname, modname);
    return 2;
  }
  
  lua_getfield(L, -1, "index");
  if(!lua_isinteger(L, -1)) {
    lua_pushnil(L);
    lua_pushfstring(L, "can't publish event '%s:%s', it hasn't been initialized", modname, evname);
    return 2;
  }
  int evindex = lua_tointeger(L, -1);
  assert(evindex >= 0);
  assert(evindex < ctx->events_count);
  
  lua_settop(L, nargs);
  
  assert(evname == ctx->events[evindex].name);
  
  const char    *datatype = ctx->events[evindex].data_type;
  void          *data;
  lua_reference_t unwrapref;
  if(nargs >=4) {
    unwrapref = luaS_unwrap_event_data(L, datatype, 4, &data);
  }
  else {
    data = NULL;
  }
  
  bool ok = shuso_event_publish(S, &ctx->events[evindex], code, data);
  
  if(nargs >= 4) {
    luaS_unwrap_event_data_cleanup(L, datatype, unwrapref, data);
  }
  
  lua_pushboolean(L, ok);
  return 1;
}

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

static int Lua_shuso_block_parent_setting_pointer(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  const shuso_setting_block_t *block = lua_topointer(L, 1);
  lua_pushlightuserdata(L, block->setting);
  return 1;
}

static int Lua_shuso_block_setting_pointer(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  luaL_checkstring(L, 2);
  
  const shuso_setting_block_t *block = lua_topointer(L, 1);
  
  luaS_get_config_pointer_ref(L, block->setting);
  if(!lua_istable(L, -1)) {
    lua_pushnil(L);
    lua_pushstring(L, "block not found");
    return 2;
  }
  
  luaS_pcall_config_method(L, "find_setting", 2, true);
  if(!lua_toboolean(L, -1)) {
    lua_pushstring(L, "setting not found");
    return 2;
  }
  lua_getfield(L, -1, "ptr");
  if(lua_type(L, -1) != LUA_TLIGHTUSERDATA) {
    lua_pushnil(L);
    lua_pushstring(L, "setting is missing ptr field");
    return 2;
  }  
  
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

static const shuso_setting_values_t *setting_values_type(lua_State *L, const shuso_setting_t *setting, int nindex) {
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
  
  if(vals == NULL) {
    lua_pushinteger(L, 0);
  }
  else {
    lua_pushinteger(L, vals->count);
  }
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
  else if(n > vals->count) {
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

static int Lua_shuso_setting_path(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  luaS_get_config_pointer_ref(L, (void *)lua_topointer(L, 1));
  assert(!lua_isnil(L, -1));
  luaS_pcall_config_method(L, "get_path", 1, true);
  return 1;
}
static int Lua_shuso_block_path(lua_State *L) {
  return Lua_shuso_setting_path(L);
}

static int Lua_shuso_match_path(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  luaL_checkstring(L, 2);
  
  luaS_get_config_pointer_ref(L, lua_topointer(L, 1));
  assert(!lua_isnil(L, -1));
  
  luaS_push_lua_module_field(L, "shuttlesock.config", "match_path");
  lua_pushvalue(L, -2);
  lua_pushvalue(L, 2);
  int top = lua_gettop(L);
  luaS_call(L, 2, LUA_MULTRET);
  return lua_gettop(L) - top;
}

static int Lua_shuso_is_light_userdata(lua_State *L) {
  return lua_type(L, 1) == LUA_TLIGHTUSERDATA;
}

static int Lua_shuso_pcall(lua_State *L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  int nargs = lua_gettop(L) - 1;
  bool ok = luaS_pcall(L, nargs, LUA_MULTRET);
  lua_pushboolean(L, ok);
  lua_insert(L, 1);
  return lua_gettop(L);
}

static int Lua_shuso_coroutine_resume(lua_State *L) {
  lua_State *coro = lua_tothread(L, 1);
  lua_remove(L, 1);
  int top = lua_gettop(L);
  
  return luaS_coroutine_resume(L, coro, top);
}

static int Lua_shuso_master_has_root(lua_State *L) {
  shuso_t *S = shuso_state(L);
  lua_pushboolean(L, S->common->master_has_root);
  return 1;
}

static int Lua_shuso_raise_signal(lua_State *L) {
  int sig = luaL_checkinteger(L, 1);
  raise(sig);
  return 0;
}

static int Lua_shuso_raise_sigabrt(lua_State *L) {
  lua_pushcfunction(L, Lua_shuso_raise_signal);
  lua_pushinteger(L, SIGABRT);
  lua_call(L, 1, 0);
  return 0;
}

static int Lua_shuso_version(lua_State *L) {
  lua_pushstring(L, SHUTTLESOCK_VERSION_STRING);
  return 1;
}

static int Lua_shuso_ipc_pack_message_data(lua_State *L) {
  assert(lua_gettop(L) == 3);
  const char *name = luaL_checkstring(L, 2);
  bool need_shmem = lua_toboolean(L, 3);
  shuso_ipc_lua_data_t *data = luaS_lua_ipc_pack_data(L, 1, name, need_shmem);
  if(data) {
    lua_pushlightuserdata(L, data);
    return 1;
  }
  else {
    lua_pushnil(L);
    lua_pushstring(L, "failed to pack message data");
    return 2;
  }
}
static int Lua_shuso_ipc_unpack_message_data(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  int top = lua_gettop(L);
  shuso_ipc_lua_data_t *data = (void *)lua_topointer(L, 1);
  if(!luaS_lua_ipc_unpack_data(L, data)) {
    lua_pushnil(L);
    lua_pushstring(L, "failed to unpack message data");
  }
  assert(lua_gettop(L) == top + 1);
  return 1;
}

static int Lua_shuso_ipc_gc_message_data(lua_State *L) {
  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  shuso_ipc_lua_data_t *data = (void *)lua_topointer(L, 1);
  bool ok = luaS_lua_ipc_gc_data(L, data);
  lua_pushboolean(L, ok);
  return 1;
}


static int pthread_mutex_lua_return(lua_State *L, int rc) {
  if(rc == 0) {
    lua_pushboolean(L, 1);
    return 1;
  }
  
  lua_pushnil(L);
  const char *err = "unknown error";
  if(rc == EINVAL) {
    err = "the calling thread's priority is higher than the mutex's current priority ceiling";
  }
  else if(rc == EBUSY) {
    err = "already locked";
  }
  else if(rc == EAGAIN) {
    err = "the maximum number of recursive locks for mutex has been exceeded";
  }
  else if(rc == EDEADLK) {
    err = "the current thread already owns the mutex";
  }
  else if(rc == EPERM) {
    err = "the current thread does not own the mutex";
  }
  lua_pushstring(L, err);
  return 2;
}

static int Lua_shuso_mutex_create(lua_State *L) {
  shuso_t             *S = shuso_state(L);
  bool                 have_mutexattr = false;
  pthread_mutexattr_t  attr;
  pthread_mutex_t     *mutex = shuso_shared_slab_alloc(&S->common->shm, sizeof(*mutex));
  if(!mutex) {
    goto fail;
  }
  if(pthread_mutexattr_init(&attr) != 0) {
    goto fail;
  }
  have_mutexattr = true;
  
  if(pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) {
    goto fail;
  }
  if(pthread_mutex_init(mutex, &attr) != 0) {
    goto fail;
  }
  pthread_mutexattr_destroy(&attr);
  
  lua_pushlightuserdata(L, (void *)mutex);
  return 1;
  
fail:
  if(have_mutexattr) {
    pthread_mutexattr_destroy(&attr);
  }
  if(mutex) {
    shuso_shared_slab_free(&S->common->shm, (void *)mutex);
  }
  
  lua_pushnil(L);
  lua_pushliteral(L, "failed to create mutex");
  return 2;
}

static int Lua_shuso_mutex_lock(lua_State *L) {
  pthread_mutex_t     *mutex = (void *)lua_topointer(L, 1);
  int rc = pthread_mutex_lock(mutex);
  return pthread_mutex_lua_return(L, rc);
}

static int Lua_shuso_mutex_trylock(lua_State *L) {
  pthread_mutex_t     *mutex = (void *)lua_topointer(L, 1);
  int rc = pthread_mutex_trylock(mutex);
  return pthread_mutex_lua_return(L, rc);
}

static int Lua_shuso_mutex_unlock(lua_State *L) {
  pthread_mutex_t     *mutex = (void *)lua_topointer(L, 1);
  int rc = pthread_mutex_unlock(mutex);
  return pthread_mutex_lua_return(L, rc);
}

static int Lua_shuso_mutex_destroy(lua_State *L) {
  pthread_mutex_t     *mutex = (void *)lua_topointer(L, 1);
  shuso_t             *S = shuso_state(L);
  int rc = pthread_mutex_destroy(mutex);
  if(rc != 0) {
    lua_pushnil(L);
    lua_pushstring(L, "failed to destroy mutex");
    return 2;
  }
  
  shuso_shared_slab_free(&S->common->shm, (void *)mutex);
  lua_pushboolean(L, 1);
  return 1;
}


static int Lua_shuso_spinlock_create(lua_State *L) {
  shuso_t *S = shuso_state(L);
  atomic_flag *spinlock = shuso_shared_slab_alloc(&S->common->shm, sizeof(*spinlock));
  
  if(!spinlock) {
    lua_pushnil(L);
    lua_pushliteral(L, "failed to allocate shared memory for spinlock");
    return 2;
  }
  atomic_flag_clear(spinlock);
  
  lua_pushlightuserdata(L, (void *)spinlock);
  return 1;
}

static int Lua_shuso_spinlock_lock(lua_State *L) {
  atomic_flag *lock = (void *)lua_topointer(L, 1);
  volatile int ctr = 0;
  while(atomic_flag_test_and_set(lock)) {
    ctr++;
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int Lua_shuso_spinlock_trylock(lua_State *L) {
  atomic_flag *lock = (void *)lua_topointer(L, 1);
  bool already_locked = atomic_flag_test_and_set(lock);
  lua_pushboolean(L, !already_locked);
  return 1;
}

static int Lua_shuso_spinlock_unlock(lua_State *L) {
  atomic_flag *lock = (void *)lua_topointer(L, 1);
  atomic_flag_clear(lock);
  lua_pushboolean(L, 1);
  return 1;
}

static int Lua_shuso_spinlock_destroy(lua_State *L) {
  shuso_t *S = shuso_state(L);
  atomic_flag *lock = (void *)lua_topointer(L, 1);
  shuso_shared_slab_free(&S->common->shm, (void *)lock);
  lua_pushboolean(L, 1);
  return 1;
}

luaL_Reg shuttlesock_core_module_methods[] = {
// creation, destruction
  {"create", Lua_shuso_create},
  {"destroy", Lua_shuso_destroy},

//configuration
  {"configure_file", Lua_shuso_configure_file},
  {"configure_string", Lua_shuso_configure_string},
  //see below for "add_module"
  {"configure_finish", Lua_shuso_configure_finish},
  
//state 
  {"shuttlesock_pointer", Lua_shuso_pointer},
  {"run", Lua_shuso_run},
  {"stop", Lua_shuso_stop},
  {"runstate", Lua_shuso_runstate},
  
//processes
  {"process_runstate", Lua_shuso_process_runstate},
  {"procnum", Lua_shuso_procnum},
  {"count_workers", Lua_shuso_count_workers},
  {"procnum_valid", Lua_shuso_procnum_valid},
  {"procnums_active", Lua_shuso_procnums_active},

//util
  {"set_log_file", Lua_shuso_set_log_fd},
  {"set_error", Lua_shuso_set_error},
  
//lua helpers
  {"is_light_userdata", Lua_shuso_is_light_userdata},
  {"pcall", Lua_shuso_pcall},
  {"coroutine_resume", Lua_shuso_coroutine_resume},

//config
  {"config_block_parent_setting_pointer", Lua_shuso_block_parent_setting_pointer},
  {"config_block_setting_pointer", Lua_shuso_block_setting_pointer},
  {"config_block_path", Lua_shuso_block_path},
  {"config_setting_block_pointer", Lua_shuso_setting_block_pointer},
  {"config_setting_path", Lua_shuso_setting_path},
  {"config_match_path", Lua_shuso_match_path},
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
  {"module_event_publish", Lua_shuso_module_event_publish},
  {"module_event_cancel", Lua_shuso_module_event_cancel},
  {"module_event_pause", Lua_shuso_module_event_pause},
  {"module_event_resume", Lua_shuso_module_event_resume},
  
//ipc
  {"send_file", Lua_shuso_ipc_send_fd},
  {"new_file_receiver", Lua_shuso_ipc_file_receiver_new},
  //{"open_listener_sockets", Lua_shuso_ipc_open_listener_sockets},
  {"ipc_send_message", luaS_ipc_send_message},
  {"ipc_pack_message_data", Lua_shuso_ipc_pack_message_data},
  {"ipc_unpack_message_data", Lua_shuso_ipc_unpack_message_data},
  {"ipc_gc_message_data", Lua_shuso_ipc_gc_message_data},

//etc
  {"version", Lua_shuso_version},
  {"master_has_root", Lua_shuso_master_has_root},
  
//for debugging
  {"raise_signal", Lua_shuso_raise_signal},
  {"raise_SIGABRT", Lua_shuso_raise_sigabrt},

  {"mutex_create", Lua_shuso_mutex_create},
  {"mutex_lock", Lua_shuso_mutex_lock},
  {"mutex_trylock", Lua_shuso_mutex_trylock},
  {"mutex_unlock", Lua_shuso_mutex_unlock},
  {"mutex_destroy", Lua_shuso_mutex_destroy},

  {"spinlock_create", Lua_shuso_spinlock_create},
  {"spinlock_lock", Lua_shuso_spinlock_lock},
  {"spinlock_trylock", Lua_shuso_spinlock_trylock},
  {"spinlock_unlock", Lua_shuso_spinlock_unlock},
  {"spinlock_destroy", Lua_shuso_spinlock_destroy},
  
  
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
  
  lua_pushvalue(L, -1);
  luaS_do_embedded_script(L, "lua_binding", 1);
  lua_pop(L, 1);
  return 1;
}

int luaS_push_system_module(lua_State *L) {
  luaL_newlib(L, shuttlesock_system_module_methods);
  return 1;
}
