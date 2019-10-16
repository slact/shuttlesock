#ifndef SHUTTLESOCK_MODULE_H
#define SHUTTLESOCK_MODULE_H
#include <shuttlesock/common.h>
#include <shuttlesock/module_event.h>
//core-facing stuff

extern shuso_module_t shuso_core_module;

struct shuso_module_setting_s {
  const char             *name;
  const char             *aliases;
  const char             *path;
  const char             *description;
  const char             *nargs;
  const char             *default_value;
  int                     block; // 0/false - no, 1/true - yes, SHUSO_SETTING_BLOCK_OPTIONAL - maybe
};// shuso_module_setting_t

struct shuso_module_s {
  const char             *name;
  const char             *version;
  const char             *parent_modules;
  shuso_module_init_fn   *initialize;
  shuso_module_config_init_fn *initialize_config;
  const char             *subscribe; //space-separated list of modname:event_name events this module may subscribe to
  const char             *publish; //space-separated list of event_names this module may publish
  void                   *privdata;
  
  
  uint8_t                *parent_modules_index_map;
  int                     index; //global module number
  shuso_module_setting_t *settings;
  void                   *events; //module struct ptr for calling events, set during intiialize_events
  struct {
    int                     count;
    shuso_module_t        **array;
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
    uint8_t                *submodule_presence_map;
#endif
  }                       submodules;
}; //shuso_module_t



struct shuso_module_context_list_s {
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  shuso_module_t *parent;
#endif
  void          **context;
}; //shuso_module_context_list_t

typedef struct {
  shuso_module_event_t configure;
  shuso_module_event_t configure_after;
  
  shuso_module_event_t start_master;
  shuso_module_event_t start_manager;
  shuso_module_event_t start_worker;
  shuso_module_event_t start_worker_before;
  shuso_module_event_t start_worker_before_lua_gxcopy;
  
  shuso_module_event_t stop_master;
  shuso_module_event_t stop_manager;
  shuso_module_event_t stop_worker;
  
  shuso_module_event_t manager_all_workers_started;
  shuso_module_event_t master_all_workers_started;
  shuso_module_event_t worker_all_workers_started;
  shuso_module_event_t worker_exited;
  shuso_module_event_t manager_exited;
  
} shuso_core_module_events_t;

struct shuso_core_module_ctx_s {
  shuso_module_context_list_t context_list;
}; //shuso_core_module_ctx_t


bool shuso_set_core_module(shuso_t *S, shuso_module_t *module);
bool shuso_add_module(shuso_t *S, shuso_module_t *module);
bool shuso_load_module(shuso_t *S, const char *filename);
bool shuso_initialize_added_modules(shuso_t *S);

//for module developers:
shuso_module_t *shuso_get_module(shuso_t *S, const char *name);
bool shuso_context_list_initialize(shuso_t *S, shuso_module_t *parent, shuso_module_context_list_t *context_list, shuso_stalloc_t *stalloc);

void *shuso_context(shuso_t *S, shuso_module_t *parent, shuso_module_t *module, shuso_module_context_list_t *context_list);
bool shuso_set_context(shuso_t *S, shuso_module_t *parent, shuso_module_t *module, void *ctx, shuso_module_context_list_t *context_list);

void *shuso_core_context(shuso_t *S,  shuso_module_t *module);
bool shuso_set_core_context(shuso_t *S, shuso_module_t *module, void *ctx);

bool shuso_core_module_event_publish(shuso_t *S, const char *name, intptr_t code, void *data);

// internal stuff
bool shuso_module_system_initialize(shuso_t *S, shuso_module_t *core_module);

#endif //SHUTTLESOCK_MODULE_H
