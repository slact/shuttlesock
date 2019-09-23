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
  runtest_module_ctx_t *ctx = &chk->ctx;
  if(S->procnum != SHUTTLESOCK_MASTER) {
    return;
  }
  ctx->timed_out = 1;
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

bool runcheck(shuso_t *S, char **err) {
  shuso_module_t        *mod = shuso_get_module(S, "runcheck");
  test_runcheck_t       *chk = mod->privdata;
  runtest_module_ctx_t  *ctx = &chk->ctx;
  if(ctx->timed_out)
    return checkfail(err, "test timed out");
  if(ctx->process.manager.exit_code != 0)
    return checkfail(err, "manager process exited with error code %i", ctx->process.manager.exit_code);
  if(ctx->process.master.exit_code != 0)
    return checkfail(err, "master process exited with error code %i", ctx->process.master.exit_code);
  if(!ctx->process.master.started)
    return checkfail(err, "master never started");
  if(!ctx->process.master.stopped)
    return checkfail(err, "master never stopped");
  if(!ctx->process.manager.started)
    return checkfail(err, "manager never started");
  if(!ctx->process.manager.stopped)
    return checkfail(err, "manager never stopped");
  
  int started_count = 0, stopped_count = 0;
  
  for(unsigned i = S->common->process.workers_start; i<= S->common->process.workers_end; i++) {
    if(!ctx->process.worker[i].started)
      return checkfail(err, "worker %i never started", i);
    if(!ctx->process.worker[i].stopped)
      return checkfail(err, "worker %i never stopped", i);
    if(ctx->process.worker[i].exit_code != 0)
      return checkfail(err, "worker %i has a bad exit code", i);
    started_count ++;
    stopped_count ++;
  }
  
  for(unsigned i = 0; i<= SHUTTLESOCK_MAX_WORKERS; i++) {
    if(ctx->process.worker[i].started)
      started_count--;
    if(ctx->process.worker[i].stopped)
      stopped_count--;
  }
  if(started_count != 0)
    return checkfail(err, "unexpected number of workers was started");
  if(stopped_count != 0)
    return checkfail(err, "unexpected number of workers was stopped");
  return true;
}

static void runcheck_event_listener(shuso_t *S, shuso_event_state_t *evs, intptr_t code, void *data, void *pd) {
  shuso_module_t  *mod = pd;
  const char      *evn = evs->name;
  test_runcheck_t *chk = mod->privdata;
  runtest_module_ctx_t *ctx = &chk->ctx;
  int              procnum = (intptr_t )data;
  shuso_log_info(S, "got event %s", evn);
  if(strcmp(evn, "core:master.start") == 0) {
    assert(ctx->process.master.started == 0);
    assert(ctx->process.master.stopped == 0);
    ctx->process.master.started = 1;
    if(ctx->timeout > 0) {
      shuso_ev_timer_init(S, &ctx->timeout_timer, ctx->timeout, 0, runcheck_timeout_timer, mod);
      shuso_ev_timer_start(S, &ctx->timeout_timer);
    }
  }
  else if(strcmp(evn, "core:manager.start") == 0) {
    assert(ctx->process.manager.started == 0);
    assert(ctx->process.manager.stopped == 0);
    ctx->process.manager.started = 1;
  }
  else if(strcmp(evn, "core:worker.start") == 0) {
    assert(ctx->process.worker[procnum].started == 0);
    assert(ctx->process.worker[procnum].stopped == 0);
    ctx->process.worker[procnum].started = 1;
  }
  else if(strcmp(evn, "core:master.stop") == 0) {
    assert(ctx->process.master.started == 1);
    assert(ctx->process.master.stopped == 0);
    ctx->process.master.stopped = 1;
  }
  else if(strcmp(evn, "core:manager.stop") == 0) {
    assert(ctx->process.manager.started == 1);
    assert(ctx->process.manager.stopped == 0);
    ctx->process.manager.stopped = 1;
  }
  else if(strcmp(evn, "core:worker.stop") == 0) {
    assert(ctx->process.worker[procnum].started == 1);
    assert(ctx->process.worker[procnum].stopped == 0);
    ctx->process.worker[procnum].stopped = 1;
  }
  else if(strcmp(evn, "core:manager.all_workers_started") == 0) {
    assert(ctx->process.all_workers_started == 0);
    ctx->process.all_workers_started = 1;
  }
  else if(strcmp(evn, "core:manager.manager_exited") == 0) {
    assert(ctx->process.manager.started == 1);
    assert(ctx->process.manager.stopped == 1);
    //TODO: add exit code and stuff
  }
  
}

static bool runcheck_module_initialize(shuso_t *S, shuso_module_t *self) {
  
  shuso_event_listen(S, "core:configure", runcheck_event_listener, self);
  shuso_event_listen(S, "core:configure.after", runcheck_event_listener, self);
      
  shuso_event_listen(S, "core:master.start", runcheck_event_listener, self);
  shuso_event_listen(S, "core:manager.start", runcheck_event_listener, self);
  shuso_event_listen(S, "core:worker.start", runcheck_event_listener, self);
  
  shuso_event_listen(S, "core:master.stop", runcheck_event_listener, self);
  shuso_event_listen(S, "core:manager.stop", runcheck_event_listener, self);
  shuso_event_listen(S, "core:worker.stop", runcheck_event_listener, self);
  
  shuso_event_listen(S, "core:manager.all_workers_started", runcheck_event_listener, self);
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
  
  chk->runcheck_module = (shuso_module_t ) {
    .name = "runcheck",
    .version = "0.0.0",
    .subscribe =
      " core:configure"
      " core:configure.after"
      
      " core:master.start"
      " core:manager.start"
      " core:worker.start"
      
      " core:master.stop"
      " core:manager.stop"
      " core:worker.stop"
      
      " core:manager.all_workers_started"
      " core:manager.worker_exited"
      " core:master.manager_exited",
    .initialize = runcheck_module_initialize,
    .privdata = chk
  };
  chk->ctx.timeout = test_timeout;
  shuso_add_module(S, &chk->runcheck_module);
  return S;
}

shuso_t *___runcheck_shuso_create(void) {
  return NULL;
}

void stop_timer(shuso_loop *loop, shuso_ev_timer *w, int revent) {
  shuso_t *S = shuso_state(loop, w);
  int desired_procnum = (intptr_t )shuso_ev_data(w);
  if(desired_procnum == SHUTTLESOCK_MASTER && S->procnum != SHUTTLESOCK_MASTER) {
    return;
  }
  if(desired_procnum == SHUTTLESOCK_MANAGER && S->procnum != SHUTTLESOCK_MANAGER) {
    return;
  }
  if(desired_procnum == SHUTTLESOCK_WORKER && S->procnum < SHUTTLESOCK_WORKER) {
    return;
  }
  shuso_stop(S, SHUSO_STOP_ASK);
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
      assert(sz < st->page.size);
      assert(sz > 0);
      chr = shuso_stalloc(st, sz);
      assert(chr != NULL);
      stats->used += sz;
      stats->count++;
      if(st->allocd.last) {
        assert(st->allocd.last->data != chr);
      }
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
