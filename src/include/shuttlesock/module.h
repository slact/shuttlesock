#ifndef SHUTTLESOCK_MODULE_H
#define SHUTTLESOCK_MODULE_H
#include <shuttlesock/common.h>
//core-facing stuff

extern shuso_module_t shuso_core_module;

struct shuso_module_s {
  const char             *name;
  const char             *version;
  const char             *parent_modules;
  shuso_module_init_fn   *initialize;
  const char             *subscribe; //space-separated list of modname:event_name events this module may subscribe to
  const char             *publish; //space-separated list of event_names this module may publish
  void                   *privdata;
  
  
  uint8_t                *parent_modules_index_map;
  int                     index; //global module number
  shuso_setting_t        *settings;
  struct {
    int                     count;
    shuso_module_t        **array;
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
    uint8_t                *submodule_presence_map;
#endif
  }                       submodules;  
}; //shuso_module_t

typedef struct {
  shuso_module_t  *module;
  shuso_module_event_fn *fn;
  void            *pd;
} shuso_module_event_listener_t;

struct shuso_module_context_list_s {
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  shuso_module_t *parent;
#endif
  void          **context;
}; //shuso_module_context_list_t

struct shuso_module_event_s {
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  size_t             count;
  _Atomic uint64_t   fired_count;
#endif
  const char        *name;
  shuso_module_event_listener_t *listeners;
}; //shuso_module_event_t

struct shuso_event_state_s {
  const shuso_module_t *publisher;
  const char           *name;
}; //shuso_event_state_t

struct shuso_core_module_ctx_s {
  struct {
    shuso_module_event_t configure;
    shuso_module_event_t configure_after;
    
    shuso_module_event_t start_master;
    shuso_module_event_t start_manager;
    shuso_module_event_t start_worker;
    
    shuso_module_event_t stop_master;
    shuso_module_event_t stop_manager;
    shuso_module_event_t stop_worker;
    
    shuso_module_event_t manager_all_workers_started;
    shuso_module_event_t master_all_workers_started;
    shuso_module_event_t worker_all_workers_started;
    shuso_module_event_t worker_exited;
    shuso_module_event_t manager_exited;
    
  }               event;
  shuso_module_context_list_t context_list;
}; //shuso_core_module_ctx_t

bool shuso_set_core_module(shuso_t *S, shuso_module_t *module);
bool shuso_add_module(shuso_t *S, shuso_module_t *module);
bool shuso_load_module(shuso_t *S, const char *filename);
bool shuso_module_finalize(shuso_t *S, shuso_module_t *module);
bool shuso_initialize_added_modules(shuso_t *S);

shuso_module_t *shuso_current_module(const shuso_t *S);
shuso_module_t *shuso_current_event(const shuso_t *S);

//for module developers:
shuso_module_t *shuso_get_module(shuso_t *S, const char *name);

void *shuso_context(shuso_t *S, shuso_module_t *parent, shuso_module_t *module, shuso_module_context_list_t *context_list);

//event stuff
bool shuso_event_initialize(shuso_t *S, shuso_module_t *mod, const char *name, shuso_module_event_t *mev);
bool shuso_event_listen(shuso_t *S, const char *name, shuso_module_event_fn *callback, void *pd);
bool shuso_event_publish(shuso_t *S, shuso_module_t *publisher_module, shuso_module_event_t *event, intptr_t code, void *data);

bool shuso_core_module_event_publish(shuso_t *S, const char *name, intptr_t code, void *data);

// internal stuff
bool shuso_module_system_initialize(shuso_t *S, shuso_module_t *core_module);

#endif //SHUTTLESOCK_MODULE_H
