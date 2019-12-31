#include <shuttlesock.h>
#include "core.h"

bool shuso_core_module_event_publish(shuso_t *S, const char *name, intptr_t code, void *data) {
  shuso_core_module_ctx_t *ctx = S->common->module_ctx.core;
  shuso_module_event_t    *ev = (shuso_module_event_t *)&ctx->events;
  shuso_module_event_t    *cur;
  int n = sizeof(ctx->events)/sizeof(shuso_module_event_t);
  for(int i = 0; i < n; i++) {
    cur = &ev[i];
    if(strcmp(cur->name, name) == 0) {
      return shuso_event_publish(S, &shuso_core_module, cur, code, data);
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

static bool core_module_initialize(shuso_t *S, shuso_module_t *self) {
  shuso_core_module_ctx_t *ctx = shuso_stalloc(&S->stalloc, sizeof(*ctx));
  assert(ctx);
  
  shuso_events_initialize(S, self, (shuso_event_init_t[]){
    {"configure",       &ctx->events.configure,         NULL, false},
    {"configure.after", &ctx->events.configure_after,   NULL, false},
    
    {"master.start",    &ctx->events.start_master,      NULL, false},
    {"manager.start",   &ctx->events.start_manager,     NULL, false},
    {"worker.start",    &ctx->events.start_worker,      NULL, false},
    {"worker.start.before.lua_gxcopy",&ctx->events.start_worker_before_lua_gxcopy, "shuttlesock_state", false},
    {"worker.start.before",&ctx->events.start_worker_before, "shuttlesock_state", false},
    
    {"master.stop",     &ctx->events.stop_master,       NULL, false},
    {"manager.stop",    &ctx->events.stop_manager,      NULL, false},
    {"worker.stop",     &ctx->events.stop_worker,       NULL, false},
    
    {"manager.workers_started",   &ctx->events.manager_all_workers_started, NULL, false},
    {"master.workers_started",    &ctx->events.master_all_workers_started,  NULL, false},
    {"worker.workers_started",    &ctx->events.worker_all_workers_started,  NULL, false},
    {"manager.worker_exited",     &ctx->events.worker_exited,               NULL, false},
    {"master.manager_exited",     &ctx->events.manager_exited,              NULL, false},
    
    {"error",                     &ctx->events.error,              "string", false},
    {.name=NULL}
  });
  
  shuso_event_listen(S, "core:worker.start.before.lua_gxcopy", core_gxcopy, self);
  
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
  if(shuso_setting_string(S, workers, 1, NULL)) {
    if(!shuso_setting_string_matches(S, workers, 1, "^auto$")) {
      return shuso_config_error(S, workers, "invalid value");
    }
    S->common->config.workers = 0;
  }
  else if(shuso_setting_integer(S, workers, 1, &nworkers)) {
    if(nworkers < 0) {
      return shuso_config_error(S, workers, "invalid value");
    }
    else if(nworkers > SHUTTLESOCK_MAX_WORKERS) {
      return shuso_config_error(S, workers, "value cannot exceed %d", SHUTTLESOCK_MAX_WORKERS);
    }
    S->common->config.workers = nworkers;
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
  ,
  .initialize = core_module_initialize,
  .initialize_config = core_module_initialize_config
};

