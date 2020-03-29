#include <shuttlesock.h>
#include <shuttlesock/internal.h>
#include "core.h"

bool shuso_core_module_event_publish(shuso_t *S, const char *name, intptr_t code, void *data) {
  shuso_core_module_ctx_t *ctx = S->common->module_ctx.core;
  shuso_module_event_t    *ev = (shuso_module_event_t *)&ctx->events;
  shuso_module_event_t    *cur;
  int n = sizeof(ctx->events)/sizeof(shuso_module_event_t);
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
  return shuso_set_context(S, &shuso_core_module, module, ctx, &S->common->module_ctx.core->context_list);
}

void *shuso_core_context(shuso_t *S, shuso_module_t *module) {
  return shuso_context(S, &shuso_core_module, module, &S->common->module_ctx.core->context_list);
}

static void stop_thing_callback(shuso_loop *loop, shuso_ev_timer *timer, int events) {
  shuso_t *S = shuso_state(loop, timer);
  shuso_remove_timer_watcher(S, timer);
  if(S->procnum >= SHUTTLESOCK_WORKER) {
    #ifndef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
      ev_break(S->ev.loop, EVBREAK_ALL);
    #else
      shuso_worker_shutdown(S);
    #endif
  }
  else {
    ev_break(S->ev.loop, EVBREAK_ALL);
  }
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

static bool stop_event_interrupt_handler(shuso_t *S, shuso_module_event_t *event, shuso_event_state_t *evstate, shuso_event_interrupt_t interrupt, double *delay_sec) {
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

static bool core_module_initialize(shuso_t *S, shuso_module_t *self) {
  shuso_core_module_ctx_t *ctx = shuso_stalloc(&S->stalloc, sizeof(*ctx));
  assert(ctx);
  
  shuso_events_initialize(S, self, (shuso_event_init_t[]){
    {"configure",       &ctx->events.configure,         NULL, NULL},
    {"configure.after", &ctx->events.configure_after,   NULL, NULL},
    
    {"master.start",    &ctx->events.start_master,      NULL, NULL},
    {"manager.start",   &ctx->events.start_manager,     NULL, NULL},
    {"worker.start",    &ctx->events.start_worker,      NULL, NULL},
    {"worker.start.before.lua_gxcopy",&ctx->events.start_worker_before_lua_gxcopy, "shuttlesock_state", NULL},
    {"worker.start.before",&ctx->events.start_worker_before, "shuttlesock_state", NULL},
    
    {"master.stop",     &ctx->events.stop_master,       NULL, stop_event_interrupt_handler},
    {"manager.stop",    &ctx->events.stop_manager,      NULL, stop_event_interrupt_handler},
    {"worker.stop",     &ctx->events.stop_worker,       NULL, stop_event_interrupt_handler},
    
    {"master.exit",     &ctx->events.exit_master,       NULL, NULL},
    {"manager.exit",    &ctx->events.exit_manager,      NULL, NULL},
    {"worker.exit",     &ctx->events.exit_worker,       NULL, NULL},
    
    {"manager.workers_started",   &ctx->events.manager_all_workers_started, NULL, NULL},
    {"master.workers_started",    &ctx->events.master_all_workers_started,  NULL, NULL},
    {"worker.workers_started",    &ctx->events.worker_all_workers_started,  NULL, NULL},
    {"manager.worker_exited",     &ctx->events.worker_exited,               NULL, NULL},
    {"master.manager_exited",     &ctx->events.manager_exited,              NULL, NULL},
    
    {"error",                     &ctx->events.error,              "string", NULL},
    {.name=NULL}
  });
  
  shuso_event_listen(S, "core:worker.start.before.lua_gxcopy", core_gxcopy,     self);
  shuso_event_listen_with_priority(S, "core:worker.stop",   core_worker_stop,   self, SHUTTLESOCK_LAST_PRIORITY);
  
  shuso_event_listen_with_priority(S, "core:manager.stop",  core_manager_try_stop, self, SHUTTLESOCK_FIRST_PRIORITY);
  shuso_event_listen_with_priority(S, "core:manager.stop",  core_manager_stop,  self, SHUTTLESOCK_LAST_PRIORITY);
  
  shuso_event_listen_with_priority(S, "core:master.stop",   core_master_try_stop, self, SHUTTLESOCK_FIRST_PRIORITY);
  shuso_event_listen_with_priority(S, "core:master.stop",   core_master_stop,   self, SHUTTLESOCK_LAST_PRIORITY);
  
  bool ok = shuso_context_list_initialize(S, self, &ctx->context_list, &S->stalloc);
  assert(ok);
  
  S->common->module_ctx.core = ctx;
  
  return true;
}

static bool core_module_initialize_config(shuso_t *S, shuso_module_t *module, shuso_setting_block_t *block) {
  if(!shuso_config_match_path(S, block, "/")) {
    return true;
  }
  shuso_setting_t *workers = shuso_setting(S, block, "workers");
  int              nworkers;
  if(!workers) {
    return true;
  }
  
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
    S->common->config.workers = 0;
  }
  else {
    return shuso_config_error(S, workers, "invalid value");
  }
  
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
    {0}
  },
  .subscribe = 
   " core:worker.start.before.lua_gxcopy"
   " core:master.stop"
   " core:manager.stop"
   " core:worker.stop"
  ,
  .initialize = core_module_initialize,
  .initialize_config = core_module_initialize_config
};

