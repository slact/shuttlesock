#include <stdlib.h>
#include <stdio.h>
#include <shuttlesock.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

static void shuso_cleanup_loop(shuso_t *ctx);
static void signal_watcher_cb(shuso_loop *, shuso_ev_signal *w, int revents);
static void child_watcher_cb(shuso_loop *, shuso_ev_child *w, int revents);
static bool test_features(shuso_t *ctx, const char **errmsg);


static void do_nothing(void) {}
#define init_phase_handler(ctx, phase) \
  if(!ctx->common->phase_handlers.phase) \
    ctx->common->phase_handlers.start_master = (shuso_handler_fn *)do_nothing

#define set_default_config(ctx, conf, default_val) do {\
  if(!(bool )((ctx)->common->config.conf)) { \
    (ctx)->common->config.conf = default_val; \
  } \
} while(0)

const char *shuso_process_as_string(shuso_t *ctx) {
  switch(ctx->procnum) {
    case SHUTTLESOCK_UNKNOWN_PROCESS:
      return "unknown";
    case SHUTTLESOCK_NOPROCESS:
      return "no_process";
    case SHUTTLESOCK_MASTER:
      return "master";
    case SHUTTLESOCK_MANAGER:
      return "manager";
    default:
      if(ctx->procnum >= SHUTTLESOCK_WORKER) {
        return "worker";
      }
      else {
        return "???";
      }
  }
}
shuso_t *shuso_create(unsigned int ev_loop_flags, shuso_runtime_handlers_t *handlers, shuso_config_t *config, const char **err) {
  shuso_common_t     *common_ctx = NULL;
  shuso_t            *ctx = NULL;
  bool                stalloc_initialized = false;
  bool                shm_slab_created = false;
  bool                resolver_global_initialized = false;
  const char         *errmsg = NULL;
  shuso_loop         *loop;
  
  shuso_system_initialize();
  
  if(!(resolver_global_initialized = shuso_resolver_global_init(&errmsg))) {
    goto fail;
  }
  
  if((common_ctx = calloc(1, sizeof(*common_ctx))) == NULL) {
    errmsg = "not enough memory to allocate common_ctx";
    goto fail;
  }
  if((ctx = calloc(1, sizeof(*ctx))) == NULL) {
    errmsg = "not enough memory to allocate ctx";
    goto fail;
  }
  
  // create the default loop so that we can catch SIGCHLD
  // http://pod.tst.eu/http://cvs.schmorp.de/libev/ev.pod#FUNCTIONS_CONTROLLING_EVENT_LOOPS:
  // "The default loop is the only loop that can handle ev_child watchers [...]"
  if((loop = ev_default_loop(ev_loop_flags)) == NULL) {
    errmsg = "failed to create event loop";
    goto fail;
  }
  
  *ctx = (shuso_t ){
    .procnum = SHUTTLESOCK_NOPROCESS,
    .ev.loop = loop,
    .ev.flags = ev_loop_flags,
    .common  = common_ctx
  };
  
  if(!shuso_lua_create(ctx)) {
    errmsg = "failed to create Lua VM";
    goto fail;
  }
  
  common_ctx->log.fd = fileno(stdout);
  
  if(config) {
    common_ctx->config = *config;
  }
  set_default_config(ctx, ipc.send_retry_delay, SHUTTLESOCK_CONFIG_DEFAULT_IPC_SEND_RETRY_DELAY);
  set_default_config(ctx, ipc.send_timeout, SHUTTLESOCK_CONFIG_DEFAULT_IPC_SEND_TIMEOUT);
  set_default_config(ctx, workers, shuso_system_cores_online());
  
  shm_slab_created = shuso_shared_slab_create(ctx, &ctx->common->shm, ctx->common->config.shared_slab_size, "main shuttlesock slab");
  if(!shm_slab_created) {
    errmsg = "failed to created shared memory slab";
    goto fail;
  }
  
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
  
  if(!shuso_ipc_commands_init(ctx)) {
    errmsg = "failed to initialize IPC commands";
    goto fail;
  }
  
  size_t sz = sizeof(_Atomic(shuso_process_state_t)) * (SHUTTLESOCK_MAX_WORKERS + 2);
  _Atomic(shuso_process_state_t) *states = shuso_shared_slab_calloc(&ctx->common->shm, sz);
  if(!states) {
    errmsg = "failed to allocate shared memory for process states";
    goto fail;
  }
  common_ctx->process.master.state = &states[0];
  common_ctx->process.manager.state = &states[1];
  for(int i=0; i < SHUTTLESOCK_MAX_WORKERS; i++) {
    common_ctx->process.worker[i].state = &states[i+2];
  }
  
  if(!test_features(ctx, &errmsg)) {
    goto fail;
  }
  
  stalloc_initialized = shuso_stalloc_init(&ctx->stalloc, 0);
  if(!stalloc_initialized) {
    goto fail;
  }
  assert(ctx->lua);
  return ctx;
  
fail:
  if(resolver_global_initialized) shuso_resolver_global_cleanup();
  if(stalloc_initialized) shuso_stalloc_empty(&ctx->stalloc);
  if(shm_slab_created) shuso_shared_slab_destroy(ctx, &ctx->common->shm);
  if(ctx->lua) {
    shuso_lua_destroy(ctx);
  }
  if(ctx) free(ctx);
  if(common_ctx) free(common_ctx);
  if(err) *err = errmsg;

  return NULL;
}

static bool test_features(shuso_t *ctx, const char **errmsg) {
  if(ctx->common->config.features.io_uring) {
    //TODO: set ctx->common.features.io_uring
  }
  return true;
}

bool shuso_destroy(shuso_t *ctx) {
  assert(ctx->ev.loop);
  ev_loop_destroy(ctx->ev.loop);
  shuso_stalloc_empty(&ctx->stalloc);
  shuso_shared_slab_destroy(ctx, &ctx->common->shm);
  if(ctx->procnum <= SHUTTLESOCK_MANAGER) {
    free(ctx->common);
  }
  shuso_lua_destroy(ctx);
  free(ctx);
  shuso_resolver_global_cleanup();
  return true;
}

static bool shuso_init_signal_watchers(shuso_t *ctx) {
  //attach master signal handlers

  int sigs[] = SHUTTLESOCK_WATCHED_SIGNALS;
  assert(sizeof(ctx->base_watchers.signal)/sizeof(shuso_ev_signal) >= sizeof(sigs)/sizeof(int));
  
  for(unsigned i=0; i<sizeof(sigs)/sizeof(int); i++) {
    shuso_ev_signal         *w = &ctx->base_watchers.signal[i];
    shuso_ev_signal_init(ctx, w, sigs[i], signal_watcher_cb, NULL);
    shuso_ev_signal_start(ctx, w);
  }
  
  //TODO: what else?
  return true;
}

bool shuso_spawn_manager(shuso_t *ctx) {
  int failed_worker_spawns = 0;
  pid_t pid = fork();
  if(pid > 0) {
    //master
    ctx->common->process.manager.pid = pid;
    shuso_ipc_channel_shared_start(ctx, &ctx->common->process.master);
    return true;
  }
  if(pid == -1) return false;
  
  shuso_log_debug(ctx, "starting %s...", shuso_process_as_string(ctx));
  ctx->procnum = SHUTTLESOCK_MANAGER;
  ctx->process = &ctx->common->process.manager;
  ctx->process->pid = getpid();
  *ctx->process->state = SHUSO_PROCESS_STATE_STARTING;
  shuso_ipc_channel_shared_start(ctx, &ctx->common->process.manager);
  setpgid(0, 0); // so that the shell doesn't send signals to manager and workers
  ev_loop_fork(ctx->ev.loop);
  
  ctx->common->phase_handlers.start_manager(ctx, ctx->common->phase_handlers.privdata);
  *ctx->process->state = SHUSO_PROCESS_STATE_RUNNING;
  ctx->common->process.workers_start = 0;
  ctx->common->process.workers_end = ctx->common->process.workers_start;
#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  shuso_log_notice(ctx, "SHUTTLESOCK_DEBUG_NO_WORKER_THREADS is enabled, workers will be started inside the manager without their own separate threads");
#endif
  shuso_log_notice(ctx, "started %s", shuso_process_as_string(ctx));
  for(int i=0; i<ctx->common->config.workers; i++) {
    if(shuso_spawn_worker(ctx, &ctx->common->process.worker[i])) {
      ctx->common->process.workers_end++;
    }
    else {
      failed_worker_spawns ++;
    }
  }
  return failed_worker_spawns == 0;
}

bool shuso_is_forked_manager(shuso_t *ctx) {
  return ctx->procnum == SHUTTLESOCK_MANAGER;
}

bool shuso_is_master(shuso_t *ctx) {
  return ctx->procnum == SHUTTLESOCK_MASTER;
}

static void stop_manager_timer_cb(shuso_loop *loop, shuso_ev_timer *w, int revents) {
  shuso_t           *ctx = shuso_ev_ctx(loop, w);
  shuso_stop_manager(ctx, SHUSO_STOP_ASK);
}
static void stop_master_timer_cb(shuso_loop *loop, shuso_ev_timer *w, int revents) {
  shuso_t           *ctx = shuso_ev_ctx(loop, w);
  shuso_stop(ctx, SHUSO_STOP_ASK);
}

bool shuso_stop_manager(shuso_t *ctx, shuso_stop_t forcefulness) {
  if(ctx->procnum == SHUTTLESOCK_MASTER) {
    //shuso_log_debug(ctx, "shuso_stop_manager from master");
    if(!shuso_ipc_send(ctx, &ctx->common->process.manager, SHUTTLESOCK_IPC_CMD_SHUTDOWN, (void *)(intptr_t )forcefulness)) {
      return false;
    }
    return true;
  }
  
  //shuso_log_debug(ctx, "shuso_stop_manager from manager");
  
  if(*ctx->process->state == SHUSO_PROCESS_STATE_RUNNING) {
    *ctx->process->state = SHUSO_PROCESS_STATE_STOPPING;
    shuso_ipc_send_workers(ctx, SHUTTLESOCK_IPC_CMD_SHUTDOWN, (void *)(intptr_t )forcefulness);
    shuso_add_timer_watcher(ctx, 0.1, 0.5, stop_manager_timer_cb, ctx);
  }
  if(*ctx->process->state == SHUSO_PROCESS_STATE_STOPPING) {
    bool all_stopped = true;
    if(forcefulness >= SHUSO_STOP_FORCE) {
      //kill all threads
      SHUSO_EACH_WORKER(ctx, worker) {
        //TODO: kill 'em!
      }
    }
    else {
      SHUSO_EACH_WORKER(ctx, worker) {
        all_stopped &= *worker->state == SHUSO_PROCESS_STATE_DEAD;
      }
    }
    if(all_stopped) {
      ctx->common->phase_handlers.stop_manager(ctx, ctx->common->phase_handlers.privdata);
      //TODO: deferred stopping
      ev_break(ctx->ev.loop, EVBREAK_ALL);
      return true;
    }
  }
  return true;
}

bool shuso_run(shuso_t *ctx) {
  ctx->procnum = SHUTTLESOCK_MASTER;
  ctx->process = &ctx->common->process.master;
  ctx->process->pid = getpid();
  
  const char *err = NULL;
  bool master_ipc_created = false, manager_ipc_created = false, shuso_resolver_initialized = false;
  
  shuso_log_debug(ctx, "starting %s...", shuso_process_as_string(ctx));
  
  if(!(master_ipc_created = shuso_ipc_channel_shared_create(ctx, &ctx->common->process.master))) {
    err = "failed to create shared IPC channel for master";
    goto fail;
  }
  if(!(manager_ipc_created = shuso_ipc_channel_shared_create(ctx, &ctx->common->process.manager))) {
    err = "failed to create shared IPC channel for manager";
    goto fail;
  }
  
  if(!(shuso_resolver_initialized = shuso_resolver_init(ctx, &ctx->common->config, &ctx->resolver))) {
    err = "failed to spawn manager process";
    goto fail;
  }
  
  shuso_ev_child_init(ctx, &ctx->base_watchers.child, 0, 0, child_watcher_cb, NULL);
  shuso_ev_child_start(ctx, &ctx->base_watchers.child);
  
  *ctx->process->state = SHUSO_PROCESS_STATE_STARTING;
  ctx->common->phase_handlers.start_master(ctx, ctx->common->phase_handlers.privdata);
  *ctx->process->state = SHUSO_PROCESS_STATE_RUNNING;
  shuso_log_notice(ctx, "started %s", shuso_process_as_string(ctx));
  if(!shuso_spawn_manager(ctx)) {
    err = "failed to spawn manager process";
    goto fail;
  }
  shuso_init_signal_watchers(ctx);
  shuso_ipc_channel_local_init(ctx);
  shuso_ipc_channel_local_start(ctx);
  ev_run(ctx->ev.loop, 0);
  shuso_log_debug(ctx, "stopping %s...", shuso_process_as_string(ctx));
  
  shuso_cleanup_loop(ctx);
  shuso_resolver_cleanup(&ctx->resolver);
  *ctx->process->state = SHUSO_PROCESS_STATE_DEAD;
  shuso_stalloc_empty(&ctx->stalloc);
  shuso_log_notice(ctx, "stopped %s", shuso_process_as_string(ctx));
  return true;
  
fail:
  if(master_ipc_created) shuso_ipc_channel_shared_destroy(ctx, &ctx->common->process.master);
  if(manager_ipc_created) shuso_ipc_channel_shared_destroy(ctx, &ctx->common->process.manager);
  if(shuso_resolver_initialized) shuso_resolver_cleanup(&ctx->resolver);
  *ctx->process->state = SHUSO_PROCESS_STATE_DEAD;
  shuso_set_error(ctx, err);
  return false;
}

bool shuso_stop(shuso_t *ctx, shuso_stop_t forcefulness) {
  if(*ctx->process->state != SHUSO_PROCESS_STATE_RUNNING && *ctx->process->state != SHUSO_PROCESS_STATE_STOPPING) {
    //no need to stop
    shuso_log_debug(ctx, "nostop");
    return false;
  }

  if(ctx->procnum != SHUTTLESOCK_MASTER) {
    return shuso_ipc_send(ctx, &ctx->common->process.master, SHUTTLESOCK_IPC_CMD_SHUTDOWN, (void *)(intptr_t )forcefulness);
  }
  
  //TODO: implement forced shutdown
  if(*ctx->process->state == SHUSO_PROCESS_STATE_RUNNING && *ctx->common->process.manager.state == SHUSO_PROCESS_STATE_RUNNING) {
    if(!shuso_stop_manager(ctx, forcefulness)) {
      return false;
    }
    shuso_add_timer_watcher(ctx, 0.1, 0.5, stop_master_timer_cb, ctx);
  }
  
  if(*ctx->process->state == SHUSO_PROCESS_STATE_RUNNING) {
    *ctx->process->state = SHUSO_PROCESS_STATE_STOPPING;
  }
  
  if(*ctx->common->process.manager.state == SHUSO_PROCESS_STATE_DEAD) {
    ctx->common->phase_handlers.stop_master(ctx, ctx->common->phase_handlers.privdata);
    //TODO: deferred stop
    ev_break(ctx->ev.loop, EVBREAK_ALL);
  }
  return true;
}

static bool shuso_worker_initialize(shuso_t *ctx) {
  assert(ctx->process);
  shuso_log_debug(ctx, "starting worker %i...", ctx->procnum);
  *ctx->process->state = SHUSO_PROCESS_STATE_STARTING;
  
  shuso_ipc_channel_shared_start(ctx, ctx->process);
  shuso_ipc_channel_local_init(ctx);
  shuso_ipc_channel_local_start(ctx);
  if(!shuso_lua_create(ctx)) {
    *ctx->process->state = SHUSO_PROCESS_STATE_DEAD;
    return false;
  }
  
  ctx->common->phase_handlers.start_worker(ctx, ctx->common->phase_handlers.privdata);
  *ctx->process->state = SHUSO_PROCESS_STATE_RUNNING;
  shuso_log_notice(ctx, "started worker %i", ctx->procnum);
  return true;
}
static void shuso_worker_shutdown(shuso_t *ctx) {
  shuso_log_debug(ctx, "stopping worker %i...", ctx->procnum);
  shuso_cleanup_loop(ctx);
  *ctx->process->state = SHUSO_PROCESS_STATE_DEAD;
#ifndef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  ev_loop_destroy(ctx->ev.loop);
#endif
  ctx->ev.loop = NULL;
  shuso_lua_destroy(ctx);
  shuso_log_notice(ctx, "stopped worker %i", ctx->procnum);
  shuso_resolver_cleanup(&ctx->resolver);
  shuso_stalloc_empty(&ctx->stalloc);
  free(ctx);
}

#ifndef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
 static void *shuso_run_worker(void *arg) {
  shuso_t   *ctx = arg;
  assert(ctx);
  
  char       threadname[16];
  snprintf(threadname, 16, "worker %i", ctx->procnum);
  shuso_system_thread_setname(threadname);
  
  ctx->ev.loop = ev_loop_new(ctx->ev.flags);
  ev_set_userdata(ctx->ev.loop, ctx);
  shuso_worker_initialize(ctx);
  
  ev_run(ctx->ev.loop, 0);
  
  shuso_worker_shutdown(ctx);
  return NULL;
}
#endif

bool shuso_spawn_worker(shuso_t *ctx, shuso_process_t *proc) {
  int               procnum = shuso_process_to_procnum(ctx, proc);
  const char       *err = NULL;
  bool              stalloc_initialized = false;
  bool              resolver_initialized = false;
  bool              shared_ipc_created = false;
  shuso_t          *workerctx = NULL;
#ifndef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  bool              pthreadattr_initialized = false;
#endif
  assert(proc);
  assert(procnum >= SHUTTLESOCK_WORKER);
  
  if(*proc->state > SHUSO_PROCESS_STATE_NIL) {
    err = "can't spawn worker here, it looks like there's a running worker already";
    goto fail;
  }
  
  workerctx = calloc(1, sizeof(*ctx));
  if(!workerctx) {
    err = "can't spawn worker: failed to malloc() shuttlesock context";
    goto fail;
  }
  
  int               prev_proc_state = *proc->state;
  *proc->state = SHUSO_PROCESS_STATE_STARTING;
  
  if(prev_proc_state == SHUSO_PROCESS_STATE_NIL) {
    assert(proc->ipc.buf == NULL);
    if(!(shared_ipc_created = shuso_ipc_channel_shared_create(ctx, proc))) {
      err = "can't spawn worker: failed to create shared IPC buffer";
      goto fail;
    }
  }
  if(prev_proc_state == SHUSO_PROCESS_STATE_DEAD) {
    assert(proc->ipc.buf);
    //TODO: ensure buf is empty
  }
  
  workerctx->common = ctx->common;
  workerctx->ev.flags = ctx->ev.flags;
  workerctx->data = ctx->data;
  
  workerctx->process = proc;
  *workerctx->process->state = SHUSO_PROCESS_STATE_NIL;
  workerctx->procnum = procnum;
  if(!(stalloc_initialized = shuso_stalloc_init(&workerctx->stalloc, 0))) {
    err = "can't spawn worker: failed to create context stalloc";
    goto fail;
  }
  
  if(!(resolver_initialized = shuso_resolver_init(workerctx, &workerctx->common->config, &workerctx->resolver))) {
    err = "can't spawn worker: unable to initialize resolver";
    goto fail;
  }
  

#ifndef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  pthread_attr_t    pthread_attr;
  
  if(!(pthreadattr_initialized = (pthread_attr_init(&pthread_attr) == 0))) {
    err = "can't spawn worker: pthread_attr_init() failed";
    goto fail;
  }
  pthread_attr_setdetachstate(&pthread_attr, PTHREAD_CREATE_DETACHED);
  
  if(pthread_create(&proc->tid, &pthread_attr, shuso_run_worker, workerctx) != 0) {
    err = "can't spawn worker: failed to create thread";
    goto fail;
  }
#else
  workerctx->ev.loop = ctx->ev.loop;
  shuso_worker_initialize(workerctx);
#endif
  
  return true;
  
fail:
  if(shared_ipc_created) shuso_ipc_channel_shared_destroy(ctx, proc);
#ifndef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  if(pthreadattr_initialized) pthread_attr_destroy(&pthread_attr);
#endif
  if(resolver_initialized) shuso_resolver_cleanup(&ctx->resolver);
  if(stalloc_initialized) shuso_stalloc_empty(&ctx->stalloc);
  if(workerctx) free(workerctx);
  return shuso_set_error(ctx, err);
}

bool shuso_stop_worker(shuso_t *ctx, shuso_process_t *proc, shuso_stop_t forcefulness) {
  if(ctx->process == proc) { //i'm the workers that wants to stop
    if(forcefulness < SHUSO_STOP_FORCE) {
      if(*ctx->process->state == SHUSO_PROCESS_STATE_RUNNING) {
        *ctx->process->state = SHUSO_PROCESS_STATE_STOPPING;
        //TODO: defer worker stop maybe?
        shuso_log_debug(ctx, "attempting to stop worker %i", ctx->procnum);
        ctx->common->phase_handlers.stop_worker(ctx, ctx->common->phase_handlers.privdata);
#ifndef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
        ev_break(ctx->ev.loop, EVBREAK_ALL);
#else
        shuso_worker_shutdown(ctx);
#endif
        
      }
      else {
        shuso_log_debug(ctx, "already shutting down");
      }
    }
    //TODO: forced self-shutdown
    return true;
  }
  else {
    return shuso_ipc_send(ctx, proc, SHUTTLESOCK_IPC_CMD_SHUTDOWN, (void *)(intptr_t )forcefulness);
  }
}

bool shuso_set_error(shuso_t *ctx, const char *err) {
  ctx->errmsg = err;
  shuso_log_error(ctx, "%s", err);
  return false;
}

shuso_process_t *shuso_procnum_to_process(shuso_t *ctx, int procnum) {
 if(procnum < SHUTTLESOCK_MASTER || procnum > SHUTTLESOCK_MAX_WORKERS) {
   return NULL;
 }
 //negative master and manager procnums refer to their position relative 
 //to worker[] in shuso_common_t, that's why this is one line,
 return &ctx->common->process.worker[procnum]; 
}

int shuso_process_to_procnum(shuso_t *ctx, shuso_process_t *proc) {
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
  for(shuso_ev_##watcher_type##_link_t *cur = (ctx)->base_watchers.watcher_type.head, *next = NULL; cur != NULL; cur = next) { \
    next = cur->next; \
    shuso_ev_##watcher_type##_stop(ctx, &cur->data); \
    free(cur); \
  } \
  llist_init(ctx->base_watchers.watcher_type)
static void shuso_cleanup_loop(shuso_t *ctx) {
  shuso_ipc_channel_local_stop(ctx);
  shuso_ipc_channel_shared_stop(ctx, ctx->process);
  
  if(ctx->procnum < SHUTTLESOCK_WORKER) {
    int sigs[] = SHUTTLESOCK_WATCHED_SIGNALS;
    for(unsigned i=0; i<sizeof(sigs)/sizeof(int); i++) {
      shuso_ev_signal_stop(ctx, &ctx->base_watchers.signal[i]);
    }
    shuso_ev_child_stop(ctx, &ctx->base_watchers.child);
    DELETE_BASE_WATCHERS(ctx, timer);
    shuso_ipc_channel_shared_destroy(ctx, &ctx->common->process.master);
    shuso_ipc_channel_shared_destroy(ctx, &ctx->common->process.manager);
  }
  if(ctx->procnum == SHUTTLESOCK_MANAGER) {
    SHUSO_EACH_WORKER(ctx, worker) {
      shuso_ipc_channel_shared_destroy(ctx, worker);
    }
  }
}
#undef DELETE_BASE_WATCHERS

static void signal_watcher_cb(shuso_loop *loop, shuso_ev_signal *w, int revents) {
  shuso_t *ctx = shuso_ev_ctx(loop, w);
  int      signum = w->ev.signum;
  shuso_log_debug(ctx, "got signal: %d", signum);
  if(ctx->procnum != SHUTTLESOCK_MASTER) {
    shuso_log_debug(ctx, "forward signal to master via IPC");
    shuso_ipc_send(ctx, &ctx->common->process.master, SHUTTLESOCK_IPC_CMD_SIGNAL, (void *)(intptr_t )signum);
  }
  else {
    //TODO: do the actual shutdown, ya know?
    switch(signum) {
      case SIGINT:
      case SIGTERM:
        shuso_stop(ctx, SHUSO_STOP_ASK);
        break;
      default:
        shuso_log_debug(ctx, "ignore signal %d", signum);
    }
  }
}

static void child_watcher_cb(shuso_loop *loop, shuso_ev_child *w, int revents) {
  shuso_t *ctx = shuso_ev_ctx(loop, w);
  if(ctx->procnum == SHUTTLESOCK_MASTER) {
    if(*ctx->common->process.manager.state == SHUSO_PROCESS_STATE_STOPPING) {
      assert(w->ev.rpid == ctx->common->process.manager.pid);
      *ctx->common->process.manager.state = SHUSO_PROCESS_STATE_DEAD;
    }
    else {
      //TODO: was that the manager that just died? if so, restart it.
    }
  }
  shuso_log_debug(ctx, "child watcher: child pid %d rstatus %x", w->ev.rpid, w->ev.rstatus);
}

bool shuso_set_log_fd(shuso_t *ctx, int fd) {
  if(ctx->procnum == SHUTTLESOCK_MASTER && *ctx->common->process.manager.state >= SHUSO_PROCESS_STATE_RUNNING) {
    shuso_ipc_send(ctx, &ctx->common->process.manager, SHUTTLESOCK_IPC_CMD_SET_LOG_FD, (void *)(intptr_t)fd);
  }
  else if(*ctx->common->process.master.state >= SHUSO_PROCESS_STATE_RUNNING) {
    shuso_ipc_send(ctx, &ctx->common->process.master, SHUTTLESOCK_IPC_CMD_SET_LOG_FD, (void *)(intptr_t)fd);
  }
  ctx->common->log.fd = fd;
  return true;
}

void shuso_listen(shuso_t *ctx, shuso_hostinfo_t *bind, shuso_handler_fn handler, shuso_handler_fn cleanup, void *pd) {
  assert(ctx->procnum == SHUTTLESOCK_MASTER);
  
  
}
