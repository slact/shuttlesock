#include "test.h"

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
  
  return ctx;
}
