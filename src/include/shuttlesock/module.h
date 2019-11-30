#ifndef SHUTTLESOCK_MODULE_H
#define SHUTTLESOCK_MODULE_H
#include <shuttlesock/common.h>
#include <shuttlesock/module_event.h>
//core-facing stuff

#define SHUSO_MODULE_INDEX_INVALID 255

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
  
  //internal stuff, don't use it unless you're a shuttlesock developer
  uint8_t                *parent_modules_index_map;
  int                     index; //global module number
  shuso_module_setting_t *settings;
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


bool shuso_add_module(shuso_t *S, shuso_module_t *module);
bool shuso_load_module(shuso_t *S, const char *filename);
bool shuso_initialize_added_modules(shuso_t *S);

//for module developers:
shuso_module_t *shuso_get_module(shuso_t *S, const char *name);
bool shuso_context_list_initialize(shuso_t *S, shuso_module_t *parent, shuso_module_context_list_t *context_list, shuso_stalloc_t *stalloc);

void *shuso_context(shuso_t *S, shuso_module_t *parent, shuso_module_t *module, shuso_module_context_list_t *context_list);
bool shuso_set_context(shuso_t *S, shuso_module_t *parent, shuso_module_t *module, void *ctx, shuso_module_context_list_t *context_list);

// internal stuff
bool shuso_add_core_modules(shuso_t *S, char *errbuf, size_t errbuflen); //used during initialization

#endif //SHUTTLESOCK_MODULE_H
