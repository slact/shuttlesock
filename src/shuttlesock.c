#include <stdlib.h>
#include <stdio.h>
#include <shuttlesock.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>

static void shuso_cleanup_loop(shuso_t *S);
static void signal_watcher_cb(shuso_loop *, shuso_ev_signal *w, int revents);
static void child_watcher_cb(shuso_loop *, shuso_ev_child *w, int revents);
static bool test_features(shuso_t *S, const char **errmsg);

int shuttlesock_watched_signals[] = SHUTTLESOCK_WATCHED_SIGNALS;

static void do_nothing(void) {}
#define init_phase_handler(S, phase) \
  if(!S->common->phase_handlers.phase) \
    S->common->phase_handlers.start_master = (shuso_handler_fn *)do_nothing

#define set_default_config(S, conf, default_val) do {\
  if(!(bool )((S)->common->config.conf)) { \
    (S)->common->config.conf = default_val; \
  } \
} while(0)

const char *shuso_process_as_string(shuso_t *S) {
  switch(S->procnum) {
    case SHUTTLESOCK_UNKNOWN_PROCESS:
      return "unknown";
    case SHUTTLESOCK_NOPROCESS:
      return "no_process";
    case SHUTTLESOCK_MASTER:
      return "master";
    case SHUTTLESOCK_MANAGER:
      return "manager";
    default:
      if(S->procnum >= SHUTTLESOCK_WORKER) {
        return "worker";
      }
      else {
        return "???";
      }
  }
}
shuso_t *shuso_create(const char **err) {
  return shuso_create_with_lua(NULL, err);
}
shuso_t *shuso_create_with_lua(lua_State *lua, const char **err) {
  shuso_common_t     *common_ctx = NULL;
  shuso_t            *S = NULL;
  bool                stalloc_initialized = false;
  bool                resolver_global_initialized = false;
  const char         *errmsg = NULL;
  
  shuso_system_initialize();
  
  if(!(resolver_global_initialized = shuso_resolver_global_init(&errmsg))) {
    goto fail;
  }
  
  if((common_ctx = calloc(1, sizeof(*common_ctx))) == NULL) {
    errmsg = "not enough memory to allocate common_ctx";
    goto fail;
  }
  if((S = calloc(1, sizeof(*S))) == NULL) {
    errmsg = "not enough memory to allocate S";
    goto fail;
  }
  
  *S = (shuso_t ){
    .procnum = SHUTTLESOCK_NOPROCESS,
    .ev.loop = NULL,
    .ev.flags = EVFLAG_AUTO,
    .config.ready = false,
    .common  = common_ctx
  };
  if(lua) {
      S->lua.state = lua;
      S->lua.external = true;
  }
  else if(!shuso_lua_create(S)) {
    errmsg = "failed to create Lua VM";
    goto fail;
  }
  
  common_ctx->log.fd = fileno(stdout);
  
  stalloc_initialized = shuso_stalloc_init(&S->stalloc, 0);
  if(!stalloc_initialized) {
    goto fail;
  }
  return S;
  
fail:
  if(resolver_global_initialized) shuso_resolver_global_cleanup();
  if(stalloc_initialized) shuso_stalloc_empty(&S->stalloc);
  if(S && S->lua.state && !S->lua.external) {
    shuso_lua_destroy(S);
  }
  if(S) free(S);
  if(common_ctx) free(common_ctx);
  if(err) *err = errmsg;

  return NULL;
}

bool shuso_configure_handlers(shuso_t *S, const shuso_runtime_handlers_t *handlers) {
  S->common->phase_handlers = *handlers;
  return true;
}

bool shuso_configure_finish(shuso_t *S) {
  bool             shm_slab_created = false;
  const char      *errmsg;
  // create the default loop so that we can catch SIGCHLD
  // http://pod.tst.eu/http://cvs.schmorp.de/libev/ev.pod#FUNCTIONS_CONTROLLING_EVENT_LOOPS:
  // "The default loop is the only loop that can handle ev_child watchers [...]"
  if((S->ev.loop = ev_default_loop(S->ev.flags)) == NULL) {
    errmsg = "failed to create event loop";
    goto fail;
  }
  set_default_config(S, ipc.send_retry_delay, SHUTTLESOCK_CONFIG_DEFAULT_IPC_SEND_RETRY_DELAY);
  set_default_config(S, ipc.send_timeout, SHUTTLESOCK_CONFIG_DEFAULT_IPC_SEND_TIMEOUT);
  set_default_config(S, workers, shuso_system_cores_online());
  
  shm_slab_created = shuso_shared_slab_create(S, &S->common->shm, S->common->config.shared_slab_size, "main shuttlesock slab");
  if(!shm_slab_created) {
    errmsg = "failed to created shared memory slab";
    goto fail;
  }
  
  init_phase_handler(S, start_master);
  init_phase_handler(S, stop_master);
  init_phase_handler(S, start_manager);
  init_phase_handler(S, stop_manager);
  init_phase_handler(S, start_worker);
  init_phase_handler(S, stop_worker);
  
  ev_set_userdata(S->ev.loop, S);
   
  if(!shuso_ipc_commands_init(S)) {
    errmsg = "failed to initialize IPC commands";
    goto fail;
  }
  
  size_t sz = sizeof(_Atomic(shuso_process_state_t)) * (SHUTTLESOCK_MAX_WORKERS + 2);
  _Atomic(shuso_process_state_t) *states = shuso_shared_slab_calloc(&S->common->shm, sz);
  if(!states) {
    errmsg = "failed to allocate shared memory for process states";
    goto fail;
  }
  S->common->process.master.state = &states[0];
  S->common->process.manager.state = &states[1];
  for(int i=0; i < SHUTTLESOCK_MAX_WORKERS; i++) {
    S->common->process.worker[i].state = &states[i+2];
  }
  
  if(!test_features(S, &errmsg)) {
    goto fail;
  }
  
  assert(S->lua.state);
  if(!shuso_lua_initialize(S)) {
    goto fail;
  }
  S->config.ready = true;
  return true;
  
fail:
  if(shm_slab_created) shuso_shared_slab_destroy(S, &S->common->shm);
  return false;
}

static bool test_features(shuso_t *S, const char **errmsg) {
  if(S->common->config.features.io_uring) {
    //TODO: set S->common.features.io_uring
  }
  return true;
}

bool shuso_destroy(shuso_t *S) {
  assert(S->ev.loop);
  ev_loop_destroy(S->ev.loop);
  shuso_stalloc_empty(&S->stalloc);
  shuso_shared_slab_destroy(S, &S->common->shm);
  if(S->procnum <= SHUTTLESOCK_MANAGER) {
    free(S->common);
  }
  shuso_lua_destroy(S);
  free(S);
  shuso_resolver_global_cleanup();
  return true;
}

static bool shuso_init_signal_watchers(shuso_t *S) {
  //attach master signal handlers

  assert(sizeof(S->base_watchers.signal)/sizeof(shuso_ev_signal) >= sizeof(shuttlesock_watched_signals)/sizeof(int));
  
  for(unsigned i=0; i<sizeof(shuttlesock_watched_signals)/sizeof(int); i++) {
    shuso_ev_signal         *w = &S->base_watchers.signal[i];
    shuso_ev_signal_init(S, w, shuttlesock_watched_signals[i], signal_watcher_cb, NULL);
    shuso_ev_signal_start(S, w);
  }
  
  //TODO: what else?
  return true;
}

bool shuso_spawn_manager(shuso_t *S) {
  int failed_worker_spawns = 0;
  
  if(S->procnum == SHUTTLESOCK_MANAGER) {
    return shuso_set_error(S, "can't spawn manager from manager");
  }
  else if(S->procnum >= SHUTTLESOCK_WORKER) {
    return shuso_set_error(S, "can't spawn manager from worker");
  }
  
  pid_t pid = fork();
  if(pid > 0) {
    //master
    S->common->process.manager.pid = pid;
    shuso_ipc_channel_shared_start(S, &S->common->process.master);
    return true;
  }
  if(pid == -1) return false;
  
  shuso_log_debug(S, "starting %s...", shuso_process_as_string(S));
  S->procnum = SHUTTLESOCK_MANAGER;
  S->process = &S->common->process.manager;
  S->process->pid = getpid();
  *S->process->state = SHUSO_PROCESS_STATE_STARTING;
  shuso_ipc_channel_shared_start(S, &S->common->process.manager);
  setpgid(0, 0); // so that the shell doesn't send signals to manager and workers
  ev_loop_fork(S->ev.loop);
  
  S->common->phase_handlers.start_manager(S, S->common->phase_handlers.privdata);
  *S->process->state = SHUSO_PROCESS_STATE_RUNNING;
  S->common->process.workers_start = 0;
  S->common->process.workers_end = S->common->process.workers_start;
#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  shuso_log_notice(S, "SHUTTLESOCK_DEBUG_NO_WORKER_THREADS is enabled, workers will be started inside the manager without their own separate threads");
#endif
  shuso_log_notice(S, "started %s", shuso_process_as_string(S));
  for(int i=0; i<S->common->config.workers; i++) {
    if(shuso_spawn_worker(S, &S->common->process.worker[i])) {
      S->common->process.workers_end++;
    }
    else {
      failed_worker_spawns ++;
    }
  }
  return failed_worker_spawns == 0;
}

bool shuso_is_forked_manager(shuso_t *S) {
  return S->procnum == SHUTTLESOCK_MANAGER;
}

bool shuso_is_master(shuso_t *S) {
  return S->procnum == SHUTTLESOCK_MASTER;
}

static void stop_manager_timer_cb(shuso_loop *loop, shuso_ev_timer *w, int revents) {
  shuso_t           *S = shuso_state(loop, w);
  shuso_stop_manager(S, SHUSO_STOP_ASK);
}
static void stop_master_timer_cb(shuso_loop *loop, shuso_ev_timer *w, int revents) {
  shuso_t           *S = shuso_state(loop, w);
  shuso_stop(S, SHUSO_STOP_ASK);
}

bool shuso_stop_manager(shuso_t *S, shuso_stop_t forcefulness) {
  if(S->procnum == SHUTTLESOCK_MASTER) {
    //shuso_log_debug(S, "shuso_stop_manager from master");
    if(!shuso_ipc_send(S, &S->common->process.manager, SHUTTLESOCK_IPC_CMD_SHUTDOWN, (void *)(intptr_t )forcefulness)) {
      return false;
    }
    return true;
  }
  
  //shuso_log_debug(S, "shuso_stop_manager from manager");
  
  if(*S->process->state == SHUSO_PROCESS_STATE_RUNNING) {
    *S->process->state = SHUSO_PROCESS_STATE_STOPPING;
    shuso_ipc_send_workers(S, SHUTTLESOCK_IPC_CMD_SHUTDOWN, (void *)(intptr_t )forcefulness);
    shuso_add_timer_watcher(S, 0.1, 0.5, stop_manager_timer_cb, S);
  }
  if(*S->process->state == SHUSO_PROCESS_STATE_STOPPING) {
    bool all_stopped = true;
    if(forcefulness >= SHUSO_STOP_FORCE) {
      //kill all threads
      SHUSO_EACH_WORKER(S, worker) {
        //TODO: kill 'em!
      }
    }
    else {
      SHUSO_EACH_WORKER(S, worker) {
        all_stopped &= *worker->state == SHUSO_PROCESS_STATE_DEAD;
      }
    }
    if(all_stopped) {
      S->common->phase_handlers.stop_manager(S, S->common->phase_handlers.privdata);
      //TODO: deferred stopping
      ev_break(S->ev.loop, EVBREAK_ALL);
      return true;
    }
  }
  return true;
}

bool shuso_run(shuso_t *S) {
  S->procnum = SHUTTLESOCK_MASTER;
  S->process = &S->common->process.master;
  S->process->pid = getpid();
  
  const char *err = NULL;
  bool master_ipc_created = false, manager_ipc_created = false, shuso_resolver_initialized = false;
  
  shuso_log_debug(S, "starting %s...", shuso_process_as_string(S));
  
  if(!(master_ipc_created = shuso_ipc_channel_shared_create(S, &S->common->process.master))) {
    err = "failed to create shared IPC channel for master";
    goto fail;
  }
  if(!(manager_ipc_created = shuso_ipc_channel_shared_create(S, &S->common->process.manager))) {
    err = "failed to create shared IPC channel for manager";
    goto fail;
  }
  
  if(!(shuso_resolver_initialized = shuso_resolver_init(S, &S->common->config, &S->resolver))) {
    err = "failed to spawn manager process";
    goto fail;
  }
  
  shuso_ev_child_init(S, &S->base_watchers.child, 0, 0, child_watcher_cb, NULL);
  shuso_ev_child_start(S, &S->base_watchers.child);
  
  *S->process->state = SHUSO_PROCESS_STATE_STARTING;
  S->common->phase_handlers.start_master(S, S->common->phase_handlers.privdata);
  *S->process->state = SHUSO_PROCESS_STATE_RUNNING;
  shuso_log_notice(S, "started %s", shuso_process_as_string(S));
  if(!shuso_spawn_manager(S)) {
    err = "failed to spawn manager process";
    goto fail;
  }
  shuso_init_signal_watchers(S);
  shuso_ipc_channel_local_init(S);
  shuso_ipc_channel_local_start(S);
  ev_run(S->ev.loop, 0);
  shuso_log_debug(S, "stopping %s...", shuso_process_as_string(S));
  
  shuso_cleanup_loop(S);
  shuso_resolver_cleanup(&S->resolver);
  *S->process->state = SHUSO_PROCESS_STATE_DEAD;
  shuso_stalloc_empty(&S->stalloc);
  shuso_log_notice(S, "stopped %s", shuso_process_as_string(S));
  return true;
  
fail:
  if(master_ipc_created) shuso_ipc_channel_shared_destroy(S, &S->common->process.master);
  if(manager_ipc_created) shuso_ipc_channel_shared_destroy(S, &S->common->process.manager);
  if(shuso_resolver_initialized) shuso_resolver_cleanup(&S->resolver);
  *S->process->state = SHUSO_PROCESS_STATE_DEAD;
  shuso_set_error(S, err);
  return false;
}

bool shuso_stop(shuso_t *S, shuso_stop_t forcefulness) {
  if(*S->process->state != SHUSO_PROCESS_STATE_RUNNING && *S->process->state != SHUSO_PROCESS_STATE_STOPPING) {
    //no need to stop
    shuso_log_debug(S, "nostop");
    return false;
  }

  if(S->procnum != SHUTTLESOCK_MASTER) {
    return shuso_ipc_send(S, &S->common->process.master, SHUTTLESOCK_IPC_CMD_SHUTDOWN, (void *)(intptr_t )forcefulness);
  }
  
  //TODO: implement forced shutdown
  if(*S->process->state == SHUSO_PROCESS_STATE_RUNNING && *S->common->process.manager.state == SHUSO_PROCESS_STATE_RUNNING) {
    if(!shuso_stop_manager(S, forcefulness)) {
      return false;
    }
    shuso_add_timer_watcher(S, 0.1, 0.5, stop_master_timer_cb, S);
  }
  
  if(*S->process->state == SHUSO_PROCESS_STATE_RUNNING) {
    *S->process->state = SHUSO_PROCESS_STATE_STOPPING;
  }
  
  if(*S->common->process.manager.state == SHUSO_PROCESS_STATE_DEAD) {
    S->common->phase_handlers.stop_master(S, S->common->phase_handlers.privdata);
    //TODO: deferred stop
    ev_break(S->ev.loop, EVBREAK_ALL);
  }
  return true;
}

static bool shuso_worker_initialize(shuso_t *S) {
  assert(S->process);
  shuso_log_debug(S, "starting worker %i...", S->procnum);
  *S->process->state = SHUSO_PROCESS_STATE_STARTING;
  
  shuso_ipc_channel_shared_start(S, S->process);
  shuso_ipc_channel_local_init(S);
  shuso_ipc_channel_local_start(S);
  if(!shuso_lua_create(S)) {
    *S->process->state = SHUSO_PROCESS_STATE_DEAD;
    return false;
  }
  
  S->common->phase_handlers.start_worker(S, S->common->phase_handlers.privdata);
  *S->process->state = SHUSO_PROCESS_STATE_RUNNING;
  shuso_log_notice(S, "started worker %i", S->procnum);
  return true;
}
static void shuso_worker_shutdown(shuso_t *S) {
  shuso_log_debug(S, "stopping worker %i...", S->procnum);
  shuso_cleanup_loop(S);
  *S->process->state = SHUSO_PROCESS_STATE_DEAD;
#ifndef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  ev_loop_destroy(S->ev.loop);
#endif
  S->ev.loop = NULL;
  shuso_lua_destroy(S);
  shuso_log_notice(S, "stopped worker %i", S->procnum);
  shuso_resolver_cleanup(&S->resolver);
  shuso_stalloc_empty(&S->stalloc);
  free(S);
}

#ifndef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
 static void *shuso_run_worker(void *arg) {
  shuso_t   *S = arg;
  assert(S);
  
  char       threadname[16];
  snprintf(threadname, 16, "worker %i", S->procnum);
  shuso_system_thread_setname(threadname);
  
  S->ev.loop = ev_loop_new(S->ev.flags);
  ev_set_userdata(S->ev.loop, S);
  shuso_worker_initialize(S);
  
  ev_run(S->ev.loop, 0);
  
  shuso_worker_shutdown(S);
  return NULL;
}
#endif

bool shuso_spawn_worker(shuso_t *S, shuso_process_t *proc) {
  int               procnum = shuso_process_to_procnum(S, proc);
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
  
  if(S->procnum == SHUTTLESOCK_MASTER) {
    err = "can't spawn worker from master";
    goto fail;
  }
  else if(S->procnum >= SHUTTLESOCK_WORKER) {
    err = "can't spawn worker from another worker";
    goto fail;
  }
  
  if(*proc->state > SHUSO_PROCESS_STATE_NIL) {
    err = "can't spawn worker here, it looks like there's a running worker already";
    goto fail;
  }
  
  workerctx = calloc(1, sizeof(*S));
  if(!workerctx) {
    err = "can't spawn worker: failed to malloc() shuttlesock context";
    goto fail;
  }
  
  int               prev_proc_state = *proc->state;
  *proc->state = SHUSO_PROCESS_STATE_STARTING;
  
  if(prev_proc_state == SHUSO_PROCESS_STATE_NIL) {
    assert(proc->ipc.buf == NULL);
    if(!(shared_ipc_created = shuso_ipc_channel_shared_create(S, proc))) {
      err = "can't spawn worker: failed to create shared IPC buffer";
      goto fail;
    }
  }
  if(prev_proc_state == SHUSO_PROCESS_STATE_DEAD) {
    assert(proc->ipc.buf);
    //TODO: ensure buf is empty
  }
  
  workerctx->common = S->common;
  workerctx->ev.flags = S->ev.flags;
  workerctx->data = S->data;
  
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
  

#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  workerctx->ev.loop = S->ev.loop;
  assert(workerctx->ev.loop == ev_default_loop(0));
  shuso_worker_initialize(workerctx);
#else
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
#endif
  
  return true;
  
fail:
  if(shared_ipc_created) shuso_ipc_channel_shared_destroy(S, proc);
#ifndef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  if(pthreadattr_initialized) pthread_attr_destroy(&pthread_attr);
#endif
  if(resolver_initialized) shuso_resolver_cleanup(&S->resolver);
  if(stalloc_initialized) shuso_stalloc_empty(&S->stalloc);
  if(workerctx) free(workerctx);
  return shuso_set_error(S, err);
}

bool shuso_stop_worker(shuso_t *S, shuso_process_t *proc, shuso_stop_t forcefulness) {
  if(S->process == proc) { //i'm the workers that wants to stop
    if(forcefulness < SHUSO_STOP_FORCE) {
      if(*S->process->state == SHUSO_PROCESS_STATE_RUNNING) {
        *S->process->state = SHUSO_PROCESS_STATE_STOPPING;
        //TODO: defer worker stop maybe?
        shuso_log_debug(S, "attempting to stop worker %i", S->procnum);
        S->common->phase_handlers.stop_worker(S, S->common->phase_handlers.privdata);
#ifndef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
        ev_break(S->ev.loop, EVBREAK_ALL);
#else
        shuso_worker_shutdown(S);
#endif
        
      }
      else {
        shuso_log_debug(S, "already shutting down");
      }
    }
    //TODO: forced self-shutdown
    return true;
  }
  else {
    return shuso_ipc_send(S, proc, SHUTTLESOCK_IPC_CMD_SHUTDOWN, (void *)(intptr_t )forcefulness);
  }
}

static void shuso_set_error_vararg(shuso_t *S, const char *fmt, va_list args) {
  if(S->error.msg && S->error.allocd) {
    free(S->error.msg);
    S->error.msg = NULL;
  }
  
  int strlen = vsnprintf(NULL, 0, fmt, args);
  S->error.msg = malloc(strlen+1);
  if(!S->error.msg) {
    S->error.msg = "failed to set error: out of memory";
    S->error.allocd = false;
  }
  vsnprintf(S->error.msg, strlen, fmt, args);
  shuso_log_error(S, "%s", S->error.msg);
}

bool shuso_set_error(shuso_t *S, const char *fmt, ...) {
  va_list args;
  S->error.error_number = 0;
  va_start(args, fmt);
  shuso_set_error_vararg(S, fmt, args);
  va_end(args);
  return false;
}
bool shuso_set_error_errno(shuso_t *S, const char *fmt, ...) {
  va_list args;
  S->error.error_number = errno;
  va_start(args, fmt);
  shuso_set_error_vararg(S, fmt, args);
  va_end(args);
  return false;
}


shuso_process_t *shuso_procnum_to_process(shuso_t *S, int procnum) {
 if(procnum < SHUTTLESOCK_MASTER || procnum > SHUTTLESOCK_MAX_WORKERS) {
   return NULL;
 }
 //negative master and manager procnums refer to their position relative 
 //to worker[] in shuso_common_t, that's why this is one line,
 return &S->common->process.worker[procnum]; 
}

int shuso_process_to_procnum(shuso_t *S, shuso_process_t *proc) {
  if(proc == &S->common->process.master) {
    return SHUTTLESOCK_MASTER;
  }
  else if(proc == &S->common->process.manager) {
    return SHUTTLESOCK_MASTER;
  }
  else if(proc >= S->common->process.worker && proc < &S->common->process.worker[SHUTTLESOCK_MAX_WORKERS]) {
    return proc - S->common->process.worker;
  }
  else {
    return SHUTTLESOCK_NOPROCESS;
  }
}

#define DELETE_BASE_WATCHERS(S, watcher_type) \
  for(shuso_ev_##watcher_type##_link_t *cur = (S)->base_watchers.watcher_type.head, *next = NULL; cur != NULL; cur = next) { \
    next = cur->next; \
    shuso_ev_##watcher_type##_stop(S, &cur->data); \
    free(cur); \
  } \
  llist_init(S->base_watchers.watcher_type)
static void shuso_cleanup_loop(shuso_t *S) {
  shuso_ipc_channel_local_stop(S);
  shuso_ipc_channel_shared_stop(S, S->process);
  
  if(S->procnum < SHUTTLESOCK_WORKER) {
    for(unsigned i=0; i<sizeof(shuttlesock_watched_signals)/sizeof(int); i++) {
      shuso_ev_signal_stop(S, &S->base_watchers.signal[i]);
    }
    shuso_ev_child_stop(S, &S->base_watchers.child);
    DELETE_BASE_WATCHERS(S, timer);
    shuso_ipc_channel_shared_destroy(S, &S->common->process.master);
    shuso_ipc_channel_shared_destroy(S, &S->common->process.manager);
  }
  if(S->procnum == SHUTTLESOCK_MANAGER) {
    SHUSO_EACH_WORKER(S, worker) {
      shuso_ipc_channel_shared_destroy(S, worker);
    }
  }
}
#undef DELETE_BASE_WATCHERS

bool shuso_setsockopt(shuso_t *S, int fd, shuso_sockopt_t *opt) {
  bool found = false;
  shuso_system_sockopts_t *known;
  for(known = &shuso_system_sockopts[0]; known->str != NULL; known++) {
    if(opt->level == known->level && opt->name == known->name) {
      found = true;
      break;
    }
  }
  if(!found) {
    return shuso_set_error(S, "unknown socket option");
  }
  
  int rc = -1;
  switch(known->value_type) {
    case SHUSO_SYSTEM_SOCKOPT_MISSING:
      return shuso_set_error(S, "failed to set socket option %s: this system does not support it", known->str);
    case SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_INT:
      rc = setsockopt(fd, opt->level, opt->name, &opt->value.integer, sizeof(opt->value.integer));
      break;
    case SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG:
      rc = setsockopt(fd, opt->level, opt->name, &opt->value.flag, sizeof(opt->value.flag));
      break;
    case SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_LINGER:
      rc = setsockopt(fd, opt->level, opt->name, &opt->value.linger, sizeof(opt->value.linger));
      break;
    case SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_TIMEVAL:
      rc = setsockopt(fd, opt->level, opt->name, &opt->value.timeval, sizeof(opt->value.timeval));
      break;
  }
  if(rc != 0) {
    return shuso_set_error_errno(S, "failed to set socket option %s: %s", known->str, strerror(errno));
  }
  return true;
}

static void signal_watcher_cb(shuso_loop *loop, shuso_ev_signal *w, int revents) {
  shuso_t *S = shuso_state(loop, w);
  int      signum = w->ev.signum;
  shuso_log_debug(S, "got signal: %d", signum);
  if(S->procnum != SHUTTLESOCK_MASTER) {
    shuso_log_debug(S, "forward signal to master via IPC");
    shuso_ipc_send(S, &S->common->process.master, SHUTTLESOCK_IPC_CMD_SIGNAL, (void *)(intptr_t )signum);
  }
  else {
    //TODO: do the actual shutdown, ya know?
    switch(signum) {
      case SIGINT:
      case SIGTERM:
        shuso_stop(S, SHUSO_STOP_ASK);
        break;
      default:
        shuso_log_debug(S, "ignore signal %d", signum);
    }
  }
}

static void child_watcher_cb(shuso_loop *loop, shuso_ev_child *w, int revents) {
  shuso_t *S = shuso_state(loop, w);
  if(S->procnum == SHUTTLESOCK_MASTER) {
    if(*S->common->process.manager.state == SHUSO_PROCESS_STATE_STOPPING) {
      assert(w->ev.rpid == S->common->process.manager.pid);
      *S->common->process.manager.state = SHUSO_PROCESS_STATE_DEAD;
    }
    else {
      //TODO: was that the manager that just died? if so, restart it.
    }
  }
  shuso_log_debug(S, "child watcher: child pid %d rstatus %x", w->ev.rpid, w->ev.rstatus);
}

bool shuso_set_log_fd(shuso_t *S, int fd) {
  if(S->procnum == SHUTTLESOCK_MASTER && *S->common->process.manager.state >= SHUSO_PROCESS_STATE_RUNNING) {
    shuso_ipc_send(S, &S->common->process.manager, SHUTTLESOCK_IPC_CMD_SET_LOG_FD, (void *)(intptr_t)fd);
  }
  else if(*S->common->process.master.state >= SHUSO_PROCESS_STATE_RUNNING) {
    shuso_ipc_send(S, &S->common->process.master, SHUTTLESOCK_IPC_CMD_SET_LOG_FD, (void *)(intptr_t)fd);
  }
  S->common->log.fd = fd;
  return true;
}

void shuso_listen(shuso_t *S, shuso_hostinfo_t *bind, shuso_handler_fn handler, shuso_handler_fn cleanup, void *pd) {
  assert(S->procnum == SHUTTLESOCK_MASTER);
  //TODO
}

bool shuso_configure_file(shuso_t *S, const char *path) {
  //TODO
  return false;
}
bool shuso_configure_string(shuso_t *S, const char *str_title, const char *str) {
  //TODO
  return false;
}
