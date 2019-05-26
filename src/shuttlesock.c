#include <stdlib.h>
#include <shuttlesock.h>
#include <ev.h>
#include <assert.h>
#include <unistd.h>
#include "shuttlesock_private.h"

static bool shuso_spawn_manager(shuso_t *ctx);
static bool shuso_spawn_worker(shuso_t *ctx);

static void cleanup_master_loop(EV_P_ ev_cleanup *w, int revents);
static void signal_watcher_cb(EV_P_ ev_signal *w, int revents);
static void child_watcher_cb(EV_P_ ev_child *w, int revents);

shuso_t *shuso_create(unsigned int ev_loop_flags, shuso_handlers_t *handlers, const char **err) {
  shuso_common_t     *common_ctx;
  shuso_t            *ctx;
  struct ev_loop     *loop;
  
  if((common_ctx = calloc(1, sizeof(*common_ctx))) == NULL) {
    if(err) *err = "not enough memory to allocate common_ctx";
    return NULL;
  }
  if((ctx = calloc(1, sizeof(*ctx))) == NULL) {
    free(common_ctx);
    if(err) *err = "not enough memory to allocate ctx";
    return NULL;
  }
  
  // create the default loop so that we can catch SIGCHLD
  // http://pod.tst.eu/http://cvs.schmorp.de/libev/ev.pod#FUNCTIONS_CONTROLLING_EVENT_LOOPS:
  // "The default loop is the only loop that can handle ev_child watchers [...]"
  if((loop = ev_default_loop(ev_loop_flags)) == NULL) {
    free(ctx);
    free(common_ctx);
    if(err) *err = "failed to create event loop";
    return NULL;
  }
  
  if(handlers) {
    common_ctx->phase_handlers = *handlers;
  }
  *ctx = (shuso_t ){
    .procnum = SHUTTLESOCK_NOPROCESS,
    .loop    = loop,
    .common  = common_ctx
  };
  
  ev_set_userdata(loop, ctx);
  
  if(!shuso_ipc_channel_shared_create(ctx, &common_ctx->process.master)) {
    free(ctx);
    free(common_ctx);
    if(err) *err = "failed to create shared IPC channel for master";
    return NULL;
  }
  if(!shuso_ipc_channel_shared_create(ctx, &common_ctx->process.manager)) {
    shuso_ipc_channel_shared_destroy(ctx, &common_ctx->process.master);
    free(ctx);
    free(common_ctx);
    if(err) *err = "failed to create shared IPC channel for manager";
    return NULL;
  }
  if(!shuso_ipc_commands_init(ctx)) {
    shuso_ipc_channel_shared_destroy(ctx, &common_ctx->process.manager);
    shuso_ipc_channel_shared_destroy(ctx, &common_ctx->process.master);
    free(ctx);
    free(common_ctx);
    if(err) *err = "failed to initialize IPC commands";
    return NULL;
  }
  
  ev_cleanup_init(&ctx->loop_cleanup, cleanup_master_loop);
  
  return ctx;
}

bool shuso_destroy(shuso_t *ctx) {
  assert(ctx->loop);
  ev_loop_destroy(ctx->loop);
  if(ctx->procnum <= SHUTTLESOCK_MANAGER) {
    free(ctx->common);
  }
  free(ctx);
  return true;
}

static bool shuso_init_signal_watchers(shuso_t *ctx) {
  //attach master signal handlers
  shuso_add_signal_watcher(ctx, signal_watcher_cb, NULL, SIGTERM);
  shuso_add_signal_watcher(ctx, signal_watcher_cb, NULL, SIGINT);
  shuso_add_signal_watcher(ctx, signal_watcher_cb, NULL, SIGQUIT);
  shuso_add_signal_watcher(ctx, signal_watcher_cb, NULL, SIGHUP);
  shuso_add_signal_watcher(ctx, signal_watcher_cb, NULL, SIGUSR1);
  shuso_add_signal_watcher(ctx, signal_watcher_cb, NULL, SIGUSR2);
  shuso_add_signal_watcher(ctx, signal_watcher_cb, NULL, SIGWINCH);
  
  shuso_add_child_watcher(ctx, child_watcher_cb, NULL, 0, 0);
  //TODO: what else?
  return true;
}

static bool shuso_spawn_manager(shuso_t *ctx) {
  pid_t pid = fork();
  if(pid > 0)   return true;
  if(pid == -1) return false;
  
  ctx->procnum = SHUTTLESOCK_MANAGER;
  ctx->process = &ctx->common->process.manager;
  
  ev_loop_fork(ctx->loop);
  return true;
}

static bool shuso_spawn_worker(shuso_t *ctx) {
  return true;
}

bool shuso_run(shuso_t *ctx) {
  ctx->procnum = SHUTTLESOCK_MASTER;
  ctx->process = &ctx->common->process.master;
  bool shuso_run(shuso_t *);
  
  shuso_init_signal_watchers(ctx);
  //TODO: add master and manager IPC initialization
  
  if(ctx->common->phase_handlers.start_master) {
    ctx->common->phase_handlers.start_master(ctx, ctx->common->phase_handlers.privdata);
  }
  if(!shuso_spawn_manager(ctx)) {
    return set_error(ctx, "failed to spawn manager process");
  }
  shuso_ipc_channel_local_start(ctx);
  shuso_ipc_channel_shared_start(ctx, ctx->process);
  ev_run(ctx->loop, 0);
  return true;
}

bool set_error(shuso_t *ctx, const char *err) {
  ctx->errmsg = err;
  return false;
}

shuso_process_t *procnum_to_process(shuso_t *ctx, int procnum) {
 if(procnum < SHUTTLESOCK_MASTER || procnum > SHUTTLESOCK_MAX_WORKERS) {
   return NULL;
 }
 //negative master and manager procnums refer to their position relative 
 //to worker[] in shuso_common_t, that's why this is one line,
 return &ctx->common->process.worker[procnum]; 
}

int process_to_procnum(shuso_t *ctx, shuso_process_t *proc) {
  if(proc == &ctx->common->process.master) {
    return SHUTTLESOCK_MASTER;
  }
  else if(proc == &ctx->common->process.manager) {
    return SHUTTLESOCK_MASTER;
  }
  else if(proc >= ctx->common->process.worker && proc < &ctx->common->process.worker[SHUTTLESOCK_MAX_WORKERS]) {
    return proc - ctx->common->process.worker;
  }
  else {
    return SHUTTLESOCK_NOPROCESS;
  }
}

#define DELETE_BASE_WATCHERS(ctx, watcher_type) \
  for(ev_##watcher_type##_link_t *cur = ctx->base_watchers.watcher_type.head, *next = NULL; cur != NULL; cur = next) { \
    next = cur->next; \
    ev_##watcher_type##_stop(EV_A_ &cur->data); \
    free(cur); \
  } \
  llist_init(ctx->base_watchers.watcher_type)
static void cleanup_master_loop(EV_P_ ev_cleanup *w, int revents) {
  shuso_t *ctx = ev_userdata(EV_A);
  
  DELETE_BASE_WATCHERS(ctx, signal);
  DELETE_BASE_WATCHERS(ctx, child);
  DELETE_BASE_WATCHERS(ctx, io);
  DELETE_BASE_WATCHERS(ctx, timer);
  DELETE_BASE_WATCHERS(ctx, periodic);
}
#undef DELETE_BASE_WATCHERS

static void signal_watcher_cb(EV_P_ ev_signal *w, int revents) {
  
}

static void child_watcher_cb(EV_P_ ev_child *w, int revents) {
  
}
