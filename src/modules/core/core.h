#ifndef SHUTTLESOCK_CORE_MODULE_H
#define SHUTTLESOCK_CORE_MODULE_H

#include <shuttlesock/common.h>
#ifdef SHUTTLESOCK_HAVE_IO_URING
#include <liburing.h>
#endif

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

#ifdef SHUTTLESOCK_HAVE_IO_URING

#define SHUTTLESOCK_CORE_IO_URING_MASTER_ENTRIES 16
#define SHUTTLESOCK_CORE_IO_URING_MANAGER_ENTRIES 64
#define SHUTTLESOCK_CORE_IO_URING_CQE_BATCH_SIZE 32

bool shuso_core_io_uring_setup(shuso_t *S);
bool shuso_core_io_uring_teardown(shuso_t *S);

struct io_uring_sqe *shuso_io_uring_get_sqe(shuso_t *S);
bool shuso_io_uring_get_sqes(shuso_t *S, struct io_uring_sqe **sqes, int num_sqes);
void shuso_io_uring_submit(shuso_t *S);

#endif //SHUTTLESOCK_HAVE_IO_URING

#endif //SHUTTLESOCK_CORE_MODULE_H
