#ifndef SHUTTLESOCK_CORE_MODULE_H
#define SHUTTLESOCK_CORE_MODULE_H

#include <shuttlesock/common.h>

extern shuso_module_t shuso_core_module;

typedef struct {
  shuso_module_context_list_t context_list;
  struct {
    shuso_event_t configure;
    shuso_event_t configure_after;
    
    shuso_event_t start_master;
    shuso_event_t start_manager;
    shuso_event_t start_worker;
    shuso_event_t start_worker_before;
    shuso_event_t start_worker_before_lua_gxcopy;
    
    shuso_event_t stop_master;
    shuso_event_t stop_manager;
    shuso_event_t stop_worker;
    
    shuso_event_t exit_master;
    shuso_event_t exit_manager;
    shuso_event_t exit_worker;
    
    shuso_event_t manager_all_workers_started;
    shuso_event_t master_all_workers_started;
    shuso_event_t worker_all_workers_started;
    shuso_event_t worker_exited;
    shuso_event_t manager_exited;
    
    shuso_event_t error;
  } events;
} shuso_core_module_common_ctx_t;

typedef struct {
  shuso_module_context_list_t context_list;
} shuso_core_module_ctx_t;

bool shuso_core_event_publish(shuso_t *S, const char *name, intptr_t code, void *data);
void *shuso_core_common_context(shuso_t *S,  shuso_module_t *module);
bool shuso_set_core_common_context(shuso_t *S, shuso_module_t *module, void *ctx);

#endif //SHUTTLESOCK_CORE_MODULE_H
