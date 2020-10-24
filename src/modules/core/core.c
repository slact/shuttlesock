#include <shuttlesock.h>
#include <shuttlesock/internal.h>
#include "core.h"

bool shuso_core_event_publish(shuso_t *S, const char *name, intptr_t code, void *data) {
  shuso_core_module_common_ctx_t *ctx = S->common->ctx.core;
  shuso_event_t           *ev = (shuso_event_t *)&ctx->events;
  shuso_event_t           *cur;
  int n = sizeof(ctx->events)/sizeof(shuso_event_t);
  for(int i = 0; i < n; i++) {
    cur = &ev[i];
    if(strcmp(cur->name, name) == 0) {
      return shuso_event_publish(S, cur, code, data);
    }
  }
  return shuso_set_error(S, "failed to publish core event %s: no such event", name);
}

static void core_gxcopy(shuso_t *S, shuso_event_state_t *evs, intptr_t status, void *data, void *pd) {
  //do nothing, all modules are copied over by the lua_bridge_module
}

bool shuso_set_core_context(shuso_t *S, shuso_module_t *module, void *ctx) {
  return shuso_set_context(S, &shuso_core_module, module, ctx, &S->ctx.core.context_list);
}

bool shuso_set_core_common_context(shuso_t *S, shuso_module_t *module, void *ctx) {
  return shuso_set_context(S, &shuso_core_module, module, ctx, &S->common->ctx.core->context_list);
}

void *shuso_core_common_context(shuso_t *S, shuso_module_t *module) {
  return shuso_context(S, &shuso_core_module, module, &S->common->ctx.core->context_list);
}

static void stop_thing_callback(shuso_loop *loop, shuso_ev_timer *timer, int events) {
  shuso_t *S = shuso_state(loop, timer);
  shuso_remove_timer_watcher(S, timer);
  
#ifndef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  ev_break(S->ev.loop, EVBREAK_ALL);
#else
  if(S->procnum >= SHUTTLESOCK_WORKER) {
    shuso_worker_shutdown(S);
  }
  else {
    ev_break(S->ev.loop, EVBREAK_ALL);
  }
#endif
}

static void core_manager_try_stop(shuso_t *S, shuso_event_state_t *evs, intptr_t status, void *data, void *pd) {
  shuso_stop_t forcefulness = (shuso_stop_t )(intptr_t )data;
  assert(S->procnum == SHUTTLESOCK_MANAGER);
  //shuso_log_debug(S, "shuso_stop_manager from manager");
  
  //shuso_log_debug(S, "stop manager (current state: %s)", shuso_runstate_as_string(*S->process->state));
  if(*S->process->state == SHUSO_STATE_RUNNING) {
    *S->process->state = SHUSO_STATE_STOPPING;
    shuso_ipc_send_workers(S, SHUTTLESOCK_IPC_CMD_SHUTDOWN, (void *)(intptr_t )forcefulness);
  }
  
  assert(*S->process->state == SHUSO_STATE_STOPPING);
  
  int all_workers_dead = true;
  
  SHUSO_EACH_WORKER(S, worker) {
    if(*worker->state == SHUSO_STATE_STOPPED) {
#ifndef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
      //worker is done running or is about to finish running (within a short, bounded time)
      pthread_join(worker->tid, NULL);
#endif
      *worker->state = SHUSO_STATE_DEAD;
    }
    if (*worker->state != SHUSO_STATE_DEAD) {
      all_workers_dead = false;
    }
  }
    
  if(!all_workers_dead) {
    shuso_event_delay(S, evs, "waiting for workers to shut down", 0.1, NULL);
    return;
  }
  
  shuso_log_debug(S, "stopping manager...");
}
  
static void core_master_try_stop(shuso_t *S, shuso_event_state_t *evs, intptr_t status, void *data, void *pd) {
  bool first_try = false;
  if(*S->process->state == SHUSO_STATE_RUNNING) {
    *S->process->state = SHUSO_STATE_STOPPING;
    first_try = true;
  }
  
  if(*S->common->process.manager.state == SHUSO_STATE_RUNNING) {
    shuso_stop_manager(S, SHUSO_STOP_INSIST);
    if(!first_try) {
      shuso_log_info(S, "waiting for manager process %i to exit..", S->common->process.manager.pid);
    }
  }
  
  if(*S->common->process.manager.state != SHUSO_STATE_DEAD) {
    shuso_event_delay(S, evs, "waiting for manager to stop", 0.1, NULL);
    return;
  }
  
  if(!first_try) {
    shuso_log_info(S, "manager process %i exited", S->common->process.manager.pid);
  }
  
  shuso_log_debug(S, "stopping master...");
}

static void core_master_stop(shuso_t *S, shuso_event_state_t *evs, intptr_t status, void *data, void *pd) {
  shuso_add_timer_watcher(S, 0.0, 0.0, stop_thing_callback, NULL);
}
static void core_manager_stop(shuso_t *S, shuso_event_state_t *evs, intptr_t status, void *data, void *pd) {
  shuso_add_timer_watcher(S, 0.0, 0.0, stop_thing_callback, NULL);
}
static void core_worker_stop(shuso_t *S, shuso_event_state_t *evs, intptr_t status, void *data, void *pd) {
  shuso_add_timer_watcher(S, 0.0, 0.0, stop_thing_callback, NULL);
}

static bool stop_event_interrupt_handler(shuso_t *S, shuso_event_t *event, shuso_event_state_t *evstate, shuso_event_interrupt_t interrupt, double *delay_sec) {
  if(interrupt != SHUSO_EVENT_DELAY) {
    return false;
  }
  if(!delay_sec) {
    return false;
  }
  if(*delay_sec > 1) {
    *delay_sec = 1;
  }
  return true;
}

static bool core_module_initialize_worker(shuso_t *S, shuso_module_t *self, shuso_t *Smanager) {
  return shuso_context_list_initialize(S, self, &S->ctx.core.context_list, &S->pool);
}

static bool core_module_initialize(shuso_t *S, shuso_module_t *self) {
  shuso_core_module_common_ctx_t *common_ctx = shuso_palloc(&S->pool, sizeof(*common_ctx));
  assert(common_ctx);
  
  shuso_events_initialize(S, self, (shuso_event_init_t[]){
    {.name="configure",       .event=&common_ctx->events.configure},
    {.name="configure.after", .event=&common_ctx->events.configure_after},
    
    {.name="master.start",    .event=&common_ctx->events.start_master},
    {.name="manager.start",   .event=&common_ctx->events.start_manager},
    {.name="worker.start",    .event=&common_ctx->events.start_worker},
    {.name="worker.start.before.lua_gxcopy", .event=&common_ctx->events.start_worker_before_lua_gxcopy, .data_type="shuttlesock_state"},
    {.name="worker.start.before", .event=&common_ctx->events.start_worker_before, .data_type="shuttlesock_state"},
    
    {.name="master.stop",     .event=&common_ctx->events.stop_master,  .interrupt_handler=stop_event_interrupt_handler},
    {.name="manager.stop",    .event=&common_ctx->events.stop_manager, .interrupt_handler=stop_event_interrupt_handler},
    {.name="worker.stop",     .event=&common_ctx->events.stop_worker,  .interrupt_handler=&stop_event_interrupt_handler},
    
    {.name="master.exit",     .event=&common_ctx->events.exit_master},
    {.name="manager.exit",    .event=&common_ctx->events.exit_manager},
    {.name="worker.exit",     .event=&common_ctx->events.exit_worker},
    
    {.name="manager.workers_started",   .event=&common_ctx->events.manager_all_workers_started},
    {.name="master.workers_started",    .event=&common_ctx->events.master_all_workers_started},
    {.name="worker.workers_started",    .event=&common_ctx->events.worker_all_workers_started},
    {.name="manager.worker_exited",     .event=&common_ctx->events.worker_exited},
    {.name="master.manager_exited",     .event=&common_ctx->events.manager_exited},
    
    {.name="error",                     .event=&common_ctx->events.error,              .data_type="string"},
    {.name=NULL}
  });
  
  shuso_event_listen(S, "core:worker.start.before.lua_gxcopy", core_gxcopy,     self);
  shuso_event_listen_with_priority(S, "core:worker.stop",   core_worker_stop,   self, SHUTTLESOCK_LAST_PRIORITY);
  
  shuso_event_listen_with_priority(S, "core:manager.stop",  core_manager_try_stop, self, SHUTTLESOCK_FIRST_PRIORITY);
  shuso_event_listen_with_priority(S, "core:manager.stop",  core_manager_stop,  self, SHUTTLESOCK_LAST_PRIORITY);
  
  shuso_event_listen_with_priority(S, "core:master.stop",   core_master_try_stop, self, SHUTTLESOCK_FIRST_PRIORITY);
  shuso_event_listen_with_priority(S, "core:master.stop",   core_master_stop,   self, SHUTTLESOCK_LAST_PRIORITY);
  
  bool ok = shuso_context_list_initialize(S, self, &common_ctx->context_list, &S->pool);
  assert(ok);
  
  S->common->ctx.core = common_ctx;
  
  ok = shuso_context_list_initialize(S, self, &S->ctx.core.context_list, &S->pool);
  assert(ok);
  
  return true;
}

static bool core_module_initialize_config(shuso_t *S, shuso_module_t *module, shuso_setting_block_t *block) {
  if(!shuso_config_match_path(S, block, "/")) {
    return true;
  }
  
  shuso_setting_t *workers = shuso_setting(S, block, "workers");
  int              nworkers;
  if(shuso_setting_integer(S, workers, 0, &nworkers)) {
    if(nworkers < 0) {
      return shuso_config_error(S, workers, "invalid value %d", nworkers);
    }
    else if(nworkers > SHUTTLESOCK_MAX_WORKERS) {
      return shuso_config_error(S, workers, "value cannot exceed %d", SHUTTLESOCK_MAX_WORKERS);
    }
    S->common->config.workers = nworkers;
  }
  else if(shuso_setting_string(S, workers, 0, NULL)) {
    if(!shuso_setting_string_matches(S, workers, 0, "^auto$")) {
      return shuso_config_error(S, workers, "invalid value");
    }
    S->common->config.workers = shuso_system_cores_online();
  }
  else {
    return shuso_config_error(S, workers, "invalid value");
  }
  
  
  shuso_setting_t  *io_uring_setting = shuso_setting(S, block, "io_uring");
  bool              io_uring_setting_val;
  if(shuso_setting_string_matches(S, workers, 0, "^auto$")) {
    S->common->config.io_uring.enabled = SHUSO_MAYBE;
  }
  if(!shuso_setting_boolean(S, workers, 0, &io_uring_setting_val)) {
    return shuso_config_error(S, io_uring_setting, "invalid value");
  }
#ifndef SHUTTLESOCK_HAVE_IO_URING
  if(io_uring_setting_val) {
    return shuso_config_error(S, io_uring_setting, "io_uring is not supported in this build of Shuttlesock");
  }
    
#endif
  S->common->config.io_uring.enabled = io_uring_setting_val;
  
  
  shuso_setting_t  *io_uring_entries = shuso_setting(S, block, "io_uring_queue_entries");
  int               entries;
  if(!shuso_setting_integer(S, io_uring_entries, 0, &entries)) {
    return shuso_config_error(S, io_uring_entries, "invalid value");
  }
  if(entries <= 0) {
    return shuso_config_error(S, io_uring_entries, "invalid value");
  }
  
  S->common->config.io_uring.worker_entries = entries;
  
  return true;
}

shuso_module_t shuso_core_module = {
  .name = "core",
  .version = SHUTTLESOCK_VERSION_STRING,
  .publish = 
   " configure"
   " configure.after"
   
   " master.start"
   " manager.start"
   " worker.start.before" //worker state created, but pthread not yet started
   " worker.start.before.lua_gxcopy" //worker state created, but pthread not yet started, luaS_gxcopy started
   " worker.start"
   
   " master.stop"
   " manager.stop"
   " worker.stop"
   
   " master.exit"
   " manager.exit"
   " worker.exit"
   
   " manager.workers_started"
   " master.workers_started"
   " worker.workers_started"
   " manager.worker_exited"
   " master.manager_exited"
   
   " error"
  ,
  .settings = (shuso_module_setting_t []) {
    {
      .name = "workers",
      .aliases = "worker_processes",
      .path = "/",
      .description = "number of workers Shuttlesock spawns. The optimal value depends things like the number of CPU cores.",
      .default_value = "auto",
      .nargs = "1"
    },
    
    {
      .name = "io_uring",
      .path = "/",
      .description = "Use the Linux kernel's io_uring I/O API",
      .default_value = "auto",
      .nargs = "1"
    },
    
    {
      .name = "io_uring_queue_entries",
      .path = "/",
      .description = "The number of io_uring queue entries.",
      .default_value = "4096",
      .nargs = "1"
    },
    
    {0}
  },
  .subscribe = 
   " core:worker.start.before"
   " core:worker.start.before.lua_gxcopy"
   " core:master.stop"
   " core:manager.stop"
   " core:worker.stop"
  ,
  .initialize = core_module_initialize,
  .initialize_worker = core_module_initialize_worker,
  .initialize_config = core_module_initialize_config
};

