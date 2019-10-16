#include "test.h"
#include <lua.h>
#include <lauxlib.h> 
#include <lualib.h>
#ifndef __clang_analyzer__

bool strmatch(const char *str, const char *pattern) {
  lua_State *L = luaL_newstate();
  luaL_openlibs(L);
  lua_pushstring(L, str);
  lua_getfield(L, -1, "match");
  lua_pushvalue(L, -2);
  lua_pushstring(L, pattern);
  lua_call(L, 2, 1);
  bool ret = lua_toboolean(L, -1);
  lua_close(L);
  return ret;
}

static void runcheck_timeout_timer(shuso_loop *loop, shuso_ev_timer *w, int revent) {
  shuso_t *S = shuso_state(loop, w);
  shuso_module_t  *mod = shuso_ev_data(w);
  test_runcheck_t *chk = mod->privdata;
  chk->timed_out = 1;
  shuso_stop(S, SHUSO_STOP_FORCE);
}

static bool checkfail(char **err, const char *fmt, ...) {
  static char errbuf[1024];
  va_list     args;
  va_start(args, fmt);
  vsnprintf(errbuf, 1024, fmt, args);
  *err = errbuf;
  va_end(args);
  return false;
}

bool ___runcheck(shuso_t *S, char **err) {
  shuso_module_t        *mod = shuso_get_module(S, "runcheck");
  test_runcheck_t       *chk = mod->privdata;
  if(chk->timed_out && !chk->timeout_is_ok)
    return checkfail(err, "test timed out");
  if(chk->process.manager.exit_code != 0)
    return checkfail(err, "manager process exited with error code %i", chk->process.manager.exit_code);
  if(chk->process.master.exit_code != 0)
    return checkfail(err, "master process exited with error code %i", chk->process.master.exit_code);
  if(!chk->process.master.started)
    return checkfail(err, "master never started");
  if(!chk->process.master.stopped)
    return checkfail(err, "master never stopped");
  if(!chk->process.manager.started)
    return checkfail(err, "manager never started");
  if(!chk->process.manager.stopped)
    return checkfail(err, "manager never stopped");

  for(unsigned i = 0; i< SHUTTLESOCK_MAX_WORKERS; i++) {
    if(chk->process.worker[i].started && !chk->process.worker[i].before_started) {
      return checkfail(err, "worker %i was started but didn't fire its start.before event", i);
    }
    if(chk->process.worker[i].started && !chk->process.worker[i].stopped) {
      return checkfail(err, "worker %i was started but never stopped", i);
    }
    if(!chk->process.worker[i].started && chk->process.worker[i].stopped) {
      return checkfail(err, "worker %i was never started but was stopped (?)", i);
    }
  }
  assert_util(chk->events.master_start == 1);
  assert_util(chk->events.manager_start == 1);
  assert_util(chk->events.worker_start >= 1);
  assert_util(chk->events.master_stop >= 1);
  assert_util(chk->events.manager_stop == 1);
  assert_util(chk->events.worker_stop >= 1);
  assert_util(chk->events.manager_workers_started == 1);
  assert_util(chk->events.master_workers_started == 1);
  assert_util(chk->events.worker_workers_started >= 1);
  assert_util(chk->events.master_manager_exited == 1);
  return true;
}

static void runcheck_event_listener(shuso_t *S, shuso_event_state_t *evs, intptr_t code, void *data, void *pd) {
  shuso_module_t  *mod = pd;
  const char      *evn = evs->name;
  test_runcheck_t *chk = mod->privdata;
  shuso_log_info(S, "got event %s", evn);
  if(strcmp(evn, "master.start") == 0) {
    assert(chk->process.master.started == 0);
    assert(chk->process.master.stopped == 0);
    chk->process.master.started = 1;
    chk->process.master.pid = S->process->pid;
    assert(chk->process.master.pid == getpid());
    if(chk->timeout > 0) {
      shuso_ev_timer_init(S, &chk->timeout_timer, chk->timeout, 0, runcheck_timeout_timer, mod);
      shuso_ev_timer_start(S, &chk->timeout_timer);
    }
    chk->events.master_start++;
  }
  else if(strcmp(evn, "manager.start") == 0) {
    assert(chk->process.manager.started == 0);
    assert(chk->process.manager.stopped == 0);
    chk->process.manager.started = 1;
    chk->process.manager.pid = S->process->pid;
    assert(chk->process.manager.pid == getpid());
    chk->events.manager_start++;
  }
  else if(strcmp(evn, "worker.start.before") == 0) {
    assert(chk->process.worker[S->procnum].before_started == 0);
    assert(chk->process.worker[S->procnum].started == 0);
    assert(chk->process.worker[S->procnum].stopped == 0);
    chk->process.worker[S->procnum].before_started = 1;
  }
  else if(strcmp(evn, "worker.start") == 0) {
    assert(chk->process.worker[S->procnum].before_started == 1);
    assert(chk->process.worker[S->procnum].started == 0);
    assert(chk->process.worker[S->procnum].stopped == 0);
    chk->process.worker[S->procnum].started = 1;
    chk->process.worker[S->procnum].pid = S->process->pid;
    assert(chk->process.worker[S->procnum].pid == getpid());
    chk->events.worker_start++;
  }
  else if(strcmp(evn, "master.stop") == 0) {
    assert(chk->process.master.started == 1);
    assert(chk->process.master.stopped == 0);
    assert(getpid() == chk->process.master.pid);
    assert(getpid() == S->process->pid);
    chk->process.master.stopped = 1;
    chk->events.master_stop++;
  }
  else if(strcmp(evn, "manager.stop") == 0) {
    assert(chk->process.manager.started == 1);
    assert(chk->process.manager.stopped == 0);
    chk->process.manager.stopped = 1;
    assert(getpid() == chk->process.manager.pid);
    assert(getpid() == S->process->pid);
    chk->events.manager_stop++;
  }
  else if(strcmp(evn, "worker.stop") == 0) {
    assert(chk->process.worker[S->procnum].before_started == 1);
    assert(chk->process.worker[S->procnum].started == 1);
    assert(chk->process.worker[S->procnum].stopped == 0);
    
    chk->process.worker[S->procnum].stopped = 1;
    assert(getpid() == chk->process.worker[S->procnum].pid);
    assert(getpid() == S->process->pid);
    chk->events.worker_stop++;
  }
  else if(strcmp(evn, "manager.workers_started") == 0) {
    assert(chk->process.all_workers_started == 0);
    chk->process.all_workers_started = 1;
    if(chk->test.run && chk->test.procnum == S->procnum) {
      chk->test.run(S, chk->test.pd);
    }
    chk->events.manager_workers_started++;
  }
  else if(strcmp(evn, "master.workers_started") == 0) {
    if(chk->test.run && chk->test.procnum == S->procnum) {
      chk->test.run(S, chk->test.pd);
    }
    chk->events.master_workers_started++;
  }
  else if(strcmp(evn, "worker.workers_started") == 0) {
    if(chk->test.run && chk->test.procnum == S->procnum) {
      chk->test.run(S, chk->test.pd);
    }
    chk->events.worker_workers_started++;
  }
  else if(strcmp(evn, "master.manager_exited") == 0) {
    pid_t rpid = code;
    shuso_sigchild_info_t *info = data;
    assert(chk->process.manager.pid == rpid);
    assert(chk->process.manager.started == 1);
    assert(chk->process.manager.stopped == 1);
    assert(info);
    switch(info->state) {
      case SHUSO_CHILD_EXITED:
        assert(info->code == 0, "manager exited with a nonzero (error) status code");
        break;
      case SHUSO_CHILD_KILLED:
        snow_fail_update();
        snow_fail("manager was killed by signal %s", shuso_system_strsignal(info->signal));
        break;
      case SHUSO_CHILD_RUNNING:
      case SHUSO_CHILD_STOPPED:
        //that's ok i think?
        break;
    }
    chk->events.master_manager_exited++;
    //TODO: add exit code and stuff
  }
  
}

static bool runcheck_module_initialize(shuso_t *S, shuso_module_t *self) {
  
  shuso_event_listen(S, "core:configure", runcheck_event_listener, self);
  shuso_event_listen(S, "core:configure.after", runcheck_event_listener, self);
  
  shuso_event_listen(S, "core:master.start", runcheck_event_listener, self);
  shuso_event_listen(S, "core:manager.start", runcheck_event_listener, self);
  shuso_event_listen(S, "core:worker.start.before", runcheck_event_listener, self);
  shuso_event_listen(S, "core:worker.start", runcheck_event_listener, self);
  
  shuso_event_listen(S, "core:master.stop", runcheck_event_listener, self);
  shuso_event_listen(S, "core:manager.stop", runcheck_event_listener, self);
  shuso_event_listen(S, "core:worker.stop", runcheck_event_listener, self);
  
  shuso_event_listen(S, "core:manager.workers_started", runcheck_event_listener, self);
  shuso_event_listen(S, "core:master.workers_started", runcheck_event_listener, self);
  shuso_event_listen(S, "core:worker.workers_started", runcheck_event_listener, self);
  shuso_event_listen(S, "core:manager.worker_exited", runcheck_event_listener, self);
  shuso_event_listen(S, "core:master.manager_exited", runcheck_event_listener, self);
  
  return true;
}

shuso_t *shusoT_create(test_runcheck_t **external_ptr, double test_timeout) {
  test_runcheck_t     *chk = shmalloc(chk);
  if(!chk) {
    snow_fail("failed to mmap test_runcheck");
    return NULL;
  }
  if(external_ptr) {
    *external_ptr = chk;
  }
  const char          *errmsg;
  shuso_t             *S = shuso_create(&errmsg);
  if(!S) {
    snow_fail("shuso_create failed: %s", errmsg);
    return NULL;
  }
  if(!test_config.verbose) {
    shuso_set_log_fd(S, dev_null);
  }
  chk->runcheck_module = (shuso_module_t ) {
    .name = "runcheck",
    .version = "0.0.0",
    .subscribe =
      " core:configure"
      " core:configure.after"
      
      " core:master.start"
      " core:manager.start"
      " core:worker.start.before"
      " core:worker.start"
      
      " core:master.stop"
      " core:manager.stop"
      " core:worker.stop"
      
      " core:manager.workers_started"
      " core:master.workers_started"
      " core:worker.workers_started"
      " core:manager.worker_exited"
      " core:master.manager_exited",
    .initialize = runcheck_module_initialize,
    .privdata = chk
  };
  chk->timeout = test_timeout;
  chk->timeout_is_ok = false;
  shuso_add_module(S, &chk->runcheck_module);
  return S;
}

bool ___shusoT_run_test(shuso_t *S, int procnum, void (*run)(shuso_t *, void *), void (*verify)(shuso_t *, void *), void *pd) {
  shuso_module_t        *mod = shuso_get_module(S, "runcheck");
  test_runcheck_t       *chk = mod->privdata;
  chk->test.run = run;
  chk->test.verify = verify;
  chk->test.pd = pd;
  chk->test.procnum = procnum;
  if(!shuso_run(S)) {
    snow_fail("shuso_run failed: %s", shuso_last_error(S));
    return false;
  }
  return true;
}

bool shusoT_destroy(shuso_t *S, test_runcheck_t **chkptr) {
  shuso_module_t        *mod = shuso_get_module(S, "runcheck");
  test_runcheck_t       *chk = mod->privdata;
  shmfree(chk);
  *chkptr = NULL;
  return shuso_destroy(S);
}

#define randrange(min, max) \
  (min + rand() / (RAND_MAX / (max - min + 1) + 1))

void fill_stalloc(shuso_stalloc_t *st, test_stalloc_stats_t *stats, size_t minsz, size_t maxsz, int large_alloc_interval, int total_items, int stack_push_count) {
  char *chr;
  int stacknum = 0;
  int stack_push_interval = total_items / stack_push_count;
  assert(stack_push_count + st->stack.count <= SHUTTLESOCK_STALLOC_STACK_SIZE);
  srand(0);
  size_t largesz = st->page.size + 10;
  stats->largesz = largesz;
  for(int i=1; i<=total_items; i++) {
    if(large_alloc_interval > 0 && i % large_alloc_interval == 0) {
      chr = shuso_stalloc(st, largesz);
      stats->used += largesz;
      memset(chr, 0x31, largesz);
      assertneq((void *)st->allocd.last->data, NULL);
      asserteq((void *)chr, (void *)st->allocd.last->data);
      stats->count++;
      stats->large++;
    }
    else {
      size_t sz = randrange(minsz, maxsz);
#ifndef SHUTTLESOCK_DEBUG_STALLOC_NOPOOL
      assert(sz < st->page.size);
#endif
      assert(sz > 0);
      chr = shuso_stalloc(st, sz);
      assert(chr != NULL);
      stats->used += sz;
      stats->count++;
#ifndef SHUTTLESOCK_DEBUG_STALLOC_NOPOOL
      if(st->allocd.last) {
        assert(st->allocd.last->data != chr);
      }
#else
      assert(st->allocd.last->data == chr);
#endif
    }
    if(stack_push_interval > 0 && i%stack_push_interval == 0) {
      int nextstack = shuso_stalloc_push(st);
      assert(nextstack > 0);
      stacknum++;
      asserteq(stacknum, nextstack);
      stats->stack_count++;
      if(nextstack <= SHUTTLESOCK_STALLOC_STACK_SIZE) {
        stats->stack[nextstack-1] = *st->stack.stack[nextstack-1];
      }
      else {
        snow_fail("stack out of bounds");
      }
    }
  }
  
}

static int open_required_module(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "__test_required_packages");
  const char *name = luaL_checkstring(L, 1);
  if(!lua_istable(L, -1)) {
    return luaL_error(L, "module %s not found", name);
  }
  lua_getfield(L, -1, name);
  if(lua_isnil(L, -1)) {
    return luaL_error(L, "module %s not found", name);
  }

  return 1;
}

void lua_add_required_module(lua_State *L, const char *name, const char *code) {
  lua_getfield(L, LUA_REGISTRYINDEX, "__test_required_packages");
  if(!lua_istable(L, -1)) {
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "__test_required_packages");
  }
  
  assert_luaL_dostring(L, code);
  lua_setfield(L, -2, name);
  
  luaL_requiref(L, name, open_required_module, false);
}

bool allocd_ptr_value_correct(char *ptr, size_t sz) {
  char chr = ((uintptr_t )ptr) % 0x100;
  for(unsigned i=0; i<sz; i++) {
    if(ptr[i] != chr) {
      return false;
    }
  }
  return true;
}

#endif //__clang_analyzer__
