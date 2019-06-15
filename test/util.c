#include "test.h"
#ifndef __clang_analyzer__

static void start_master(shuso_t *ctx, void *pd) {
  test_runcheck_t *chk = pd;
  chk->count.start_master++;
  chk->master_pid = getpid();
  //shuso_log(ctx, "==== start master %d chk %p", chk->master_pid, (void *)chk);
}
static void stop_master(shuso_t *ctx, void *pd) {
  test_runcheck_t *chk = pd;
  chk->count.stop_master++;
  //shuso_log(ctx, "==== stop master %d chk %p", chk->master_pid, (void *)chk);
}
static void start_manager(shuso_t *ctx, void *pd) {
  test_runcheck_t *chk = pd;
  chk->count.start_manager++;
  chk->manager_pid = getpid();
  //shuso_log(ctx, "==== start manager %d chk %p", chk->manager_pid, (void *)chk);
}
static void stop_manager(shuso_t *ctx, void *pd) {
  test_runcheck_t *chk = pd;
  chk->count.stop_manager++;
  //shuso_log(ctx, "==== stop manager %d chk %p", chk->manager_pid, (void *)chk);
}
static void start_worker(shuso_t *ctx, void *pd) {
  test_runcheck_t *chk = pd;
  chk->count.start_worker++;
}
static void stop_worker(shuso_t *ctx, void *pd) {
  test_runcheck_t *chk = pd;
  chk->count.stop_worker++;
}

shuso_t *___runcheck_shuso_create(unsigned int ev_loop_flags, shuso_config_t *config) {
  shuso_t             *ctx;
  const char          *err = NULL;
  test_runcheck_t     *chk = shmalloc(chk);
  if(!chk) {
    snow_fail("failed to mmap test_runcheck");
    return NULL;
  }
  shuso_handlers_t runcheck_handlers = {
    .start_master = start_master,
    .stop_master = stop_master,
    .start_manager = start_manager,
    .stop_manager = stop_manager,
    .start_worker = start_worker,
    .stop_worker = stop_worker,
    .privdata = chk,
  };
  chk->master_pid = -1;
  chk->manager_pid = -1;
  ctx = shuso_create(ev_loop_flags, &runcheck_handlers, NULL, &err);
  /*printf("RUNCHECK %p:\n"
    "master_pid: %d\n"
    "manager_pid: %d\n"
    "count.start_master: %d\n"
    "count.stop_master: %d\n"
    "count.start_manager: %d\n"
    "count.stop_manager: %d\n"
    "count.start_worker: %d\n"
    "count.stop_worker: %d\n",
    (void *)chk, chk->master_pid, chk->manager_pid, chk->count.start_master, chk->count.stop_master, chk->count.start_manager, chk->count.stop_manager, chk->count.start_worker, chk->count.stop_worker
  );*/
  if(ctx == NULL || err) {
    snow_fail("failed to create shuttlesock ctx: %s", err ? err : "unknown error");
    return NULL;
  }
  if(!test_config.verbose) {
    shuso_set_log_fd(ctx, dev_null);
  }
  
  return ctx;
}

void stop_timer(EV_P_ ev_timer *w, int revent) {
  shuso_t *ctx = ev_userdata(EV_A);
  int desired_procnum = (intptr_t )w->data;
  if(desired_procnum == SHUTTLESOCK_MASTER && ctx->procnum != SHUTTLESOCK_MASTER) {
    return;
  }
  if(desired_procnum == SHUTTLESOCK_MANAGER && ctx->procnum != SHUTTLESOCK_MANAGER) {
    return;
  }
  if(desired_procnum == SHUTTLESOCK_WORKER && ctx->procnum < SHUTTLESOCK_WORKER) {
    return;
  }
  shuso_stop(ctx, SHUSO_STOP_ASK);
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
#endif //__clang_analyzer__
