#include <stdlib.h>
#include <shuttlesock.h>
#include <ev.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include "shuttlesock_private.h"
#include <shuttlesock/log.h>
#include "sysutil.h"

static void cleanup_loop(EV_P_ ev_cleanup *w, int revents);
static void signal_watcher_cb(EV_P_ ev_signal *w, int revents);
static void child_watcher_cb(EV_P_ ev_child *w, int revents);

static void do_nothing(void) {}
#define init_phase_handler(ctx, phase) \
  if(!ctx->common->phase_handlers.phase) \
    ctx->common->phase_handlers.start_master = (shuso_callback_fn *)do_nothing

#define set_default_config(ctx, conf, default_val) do {\
  if(!(ctx)->common->config.conf) { \
    (ctx)->common->config.conf = default_val; \
  } \
} while(0)

shuso_t *shuso_create(unsigned int ev_loop_flags, shuso_handlers_t *handlers, shuso_config_t *config, const char **err) {
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
  
  *ctx = (shuso_t ){
    .procnum = SHUTTLESOCK_NOPROCESS,
    .ev.loop = loop,
    .ev.flags = ev_loop_flags,
    .common  = common_ctx
  };
  
  if(config) {
    common_ctx->config = *config;
  }
  set_default_config(ctx, ipc_buffer_size, SHUTTLESOCK_CONFIG_DEFAULT_IPC_BUFFER_SIZE);
  set_default_config(ctx, ipc_send_retry_delay, SHUTTLESOCK_CONFIG_DEFAULT_IPC_SEND_RETRY_DELAY);
  set_default_config(ctx, ipc_receive_retry_delay, SHUTTLESOCK_CONFIG_DEFAULT_IPC_RECEIVE_RETRY_DELAY);
  set_default_config(ctx, ipc_send_timeout, SHUTTLESOCK_CONFIG_DEFAULT_IPC_SEND_TIMEOUT);
  set_default_config(ctx, workers, shuso_system_cores_online());
  
  if(handlers) {
    common_ctx->phase_handlers = *handlers;
  }
  init_phase_handler(ctx, start_master);
  init_phase_handler(ctx, stop_master);
  init_phase_handler(ctx, start_manager);
  init_phase_handler(ctx, stop_manager);
  init_phase_handler(ctx, start_worker);
  init_phase_handler(ctx, stop_worker);
  
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
  
  ev_cleanup_init(&ctx->ev.cleanup, cleanup_loop);
  
  return ctx;
}

bool shuso_destroy(shuso_t *ctx) {
  assert(ctx->ev.loop);
  ev_loop_destroy(ctx->ev.loop);
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
  shuso_add_signal_watcher(ctx, signal_watcher_cb, NULL, SIGCONT);
  shuso_add_signal_watcher(ctx, signal_watcher_cb, NULL, SIGUSR1);
  shuso_add_signal_watcher(ctx, signal_watcher_cb, NULL, SIGUSR2);
  shuso_add_signal_watcher(ctx, signal_watcher_cb, NULL, SIGWINCH);

  //TODO: what else?
  return true;
}

bool shuso_spawn_manager(shuso_t *ctx) {
  pid_t pid = fork();
  if(pid > 0) {
    //master
    shuso_ipc_channel_shared_start(ctx, &ctx->common->process.master);
    return true;
  }
  if(pid == -1) return false;
  
  ctx->procnum = SHUTTLESOCK_MANAGER;
  ctx->process = &ctx->common->process.manager;
  ctx->process->id = getpid();
  ctx->process->state = SHUSO_PROCESS_STATE_STARTING;
  shuso_ipc_channel_shared_start(ctx, &ctx->common->process.manager);
  setpgid(0, 0); // so that the shell doesn't send signals to manager and workers
  ev_loop_fork(ctx->ev.loop);
  ctx->common->phase_handlers.start_manager(ctx, ctx->common->phase_handlers.privdata);
  ctx->process->state = SHUSO_PROCESS_STATE_RUNNING;
  ctx->common->process.workers_start = 0;
  ctx->common->process.workers_end = ctx->common->process.workers_start;
  for(int i=0; i<ctx->common->config.workers; i++) {
    shuso_spawn_worker(ctx, &ctx->common->process.worker[i]);
    ctx->common->process.workers_end++;
  }
  return true;
}

bool shuso_run(shuso_t *ctx) {
  ctx->procnum = SHUTTLESOCK_MASTER;
  ctx->process = &ctx->common->process.master;
  ctx->process->id = getpid();
  
  shuso_add_child_watcher(ctx, child_watcher_cb, NULL, 0, 0);
  
  ctx->process->state = SHUSO_PROCESS_STATE_STARTING;
  ctx->common->phase_handlers.start_master(ctx, ctx->common->phase_handlers.privdata);
  ctx->process->state = SHUSO_PROCESS_STATE_RUNNING;
  
  if(!shuso_spawn_manager(ctx)) {
    return set_error(ctx, "failed to spawn manager process");
  }
  shuso_init_signal_watchers(ctx);
  shuso_ipc_channel_local_start(ctx);
  ev_run(ctx->ev.loop, 0);
  shuso_log(ctx, "done running");
  return true;
}


static void shuso_cleanup_worker_thread(void *arg) {
  shuso_t     *ctx = arg;
  assert(ctx->ev.loop);
  ev_loop_destroy(ctx->ev.loop);
  free(ctx);
}

 static void *shuso_run_worker(void *arg) {
  shuso_t   *ctx = arg;
  assert(ctx);
  
  char       threadname[16];
  snprintf(threadname, 16, "worker %i", ctx->procnum);
  shuso_system_thread_setname(threadname);
  
  ctx->ev.loop = ev_loop_new(ctx->ev.flags);
  ev_set_userdata(ctx->ev.loop, ctx);
  ev_cleanup_init(&ctx->ev.cleanup, cleanup_loop);
  assert(ctx->process);
  ctx->process->state = SHUSO_PROCESS_STATE_STARTING;
  pthread_cleanup_push(shuso_cleanup_worker_thread, ctx);
  
  shuso_ipc_channel_shared_start(ctx, ctx->process);
  shuso_ipc_channel_local_start(ctx);
  
  ctx->common->phase_handlers.start_worker(ctx, ctx->common->phase_handlers.privdata);
  ctx->process->state = SHUSO_PROCESS_STATE_RUNNING;
  
  ev_run(ctx->ev.loop, 0);
  shuso_log(ctx, "done running worker?...");
  pthread_cleanup_pop(1);
  return NULL;
}

bool shuso_spawn_worker(shuso_t *ctx, shuso_process_t *proc) {
  int               procnum = process_to_procnum(ctx, proc);
  assert(proc);
  assert(procnum >= SHUTTLESOCK_WORKER);
  
  if(proc->state > SHUSO_PROCESS_STATE_NIL) {
    return set_error(ctx, "can't spawn worker here, it looks like there's a running worker already");
  }
  
  int               prev_proc_state = proc->state;
  proc->state = SHUSO_PROCESS_STATE_STARTING;
  pthread_attr_t    pthread_attr;
  if(pthread_attr_init(&pthread_attr) != 0) {
    return set_error(ctx, "can't spawn worker: pthread_attr_init() failed");
  }
  
  shuso_t          *threadctx = calloc(1, sizeof(*ctx));
  if(!threadctx) {
    pthread_attr_destroy(&pthread_attr);
    return set_error(ctx, "can't spawn worker: failed to malloc() shuttlesock context");
  }
  threadctx->common = ctx->common;
  threadctx->ev.flags = ctx->ev.flags;
  threadctx->data = ctx->data;
  
  if(prev_proc_state == SHUSO_PROCESS_STATE_NIL) {
    assert(proc->ipc.buf == NULL);
    if(!shuso_ipc_channel_shared_create(ctx, proc)) {
      pthread_attr_destroy(&pthread_attr);
      free(threadctx);
      return set_error(ctx, "can't spawn worker: failed to create shared IPC buffer");
    }
  }
  if(prev_proc_state == SHUSO_PROCESS_STATE_DEAD) {
    assert(proc->ipc.buf);
    //TODO: ensure buf is empty
  }
  
  threadctx->process = proc;
  threadctx->process->state = SHUSO_PROCESS_STATE_NIL;
  threadctx->procnum = procnum;
  
  if(pthread_create(&proc->thread, &pthread_attr, shuso_run_worker, threadctx) != 0) {
    pthread_attr_destroy(&pthread_attr);
    free(threadctx);
    return set_error(ctx, "can't spawn worker: failed to create thread");
  }
  
  return true;
}

bool shuso_stop_worker(shuso_t *ctx, shuso_process_t *proc, shuso_stop_t forcefulness) {
  ctx->common->phase_handlers.stop_worker(ctx, ctx->common->phase_handlers.privdata);
  return true;
}

bool set_error(shuso_t *ctx, const char *err) {
  ctx->errmsg = err;
  shuso_log(ctx, "%s", err);
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
static void cleanup_loop(EV_P_ ev_cleanup *w, int revents) {
  shuso_t *ctx = ev_userdata(EV_A);
  if(ctx->procnum == SHUTTLESOCK_MASTER) {
    DELETE_BASE_WATCHERS(ctx, signal);
    DELETE_BASE_WATCHERS(ctx, child);
    DELETE_BASE_WATCHERS(ctx, io);
    DELETE_BASE_WATCHERS(ctx, timer);
    DELETE_BASE_WATCHERS(ctx, periodic);
  }
}
#undef DELETE_BASE_WATCHERS

static void signal_watcher_cb(EV_P_ ev_signal *w, int revents) {
  shuso_t *ctx = ev_userdata(EV_A);
  shuso_log(ctx, "got signal: %d", w->signum);
  if(ctx->procnum != SHUTTLESOCK_MASTER) {
    shuso_log(ctx, "forward signal to master via IPC");
    shuso_ipc_send(ctx, &ctx->common->process.master, SHUTTLESOCK_IPC_CMD_SIGNAL, (void *)(intptr_t )w->signum);
  }
  else {
    //TODO: do the actual shutdown, ya know?
    shuso_ipc_send(ctx, &ctx->common->process.manager, SHUTTLESOCK_IPC_CMD_SHUTDOWN, (void *)(intptr_t )(shuso_stop_t )SHUSO_STOP_ASK);
    
  }
}

static void child_watcher_cb(EV_P_ ev_child *w, int revents) {
  shuso_t *ctx = ev_userdata(EV_A);
  shuso_log(ctx, "child watcher: (detected by %d) child pid %d rstatus %x", w->rpid, w->pid, w->rstatus);
}
