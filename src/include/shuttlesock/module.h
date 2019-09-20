#ifndef SHUTTLESOCK_MODULE_H
#define SHUTTLESOCK_MODULE_H
#include <shuttlesock/common.h>
//core-facing stuff

struct shuso_module_s {
  const char             *name;
  const char             *version;
  shuso_module_init_fn   *initialize;
  const char             *subscribe; //space-separated list of modname:event_name events this module may subscribe to
  const char             *publish; //space-separated list of event_names this module may publish
  const char             *parent_modules;
  
  int                     context_count;
  struct {
    int                     count;
    shuso_module_t        **array;
    void                  **context;
  }                       submodules;
}; //shuso_module_t

typedef struct {
  shuso_module_t  *module;
  shuso_module_event_fn *fn;
  void            *pd;
} shuso_module_event_listener_t;

struct shuso_module_event_s {
#ifdef SHUTTLESOCK_MEVENT_DEBUG
  size_t             count;
  _Atomic uint64_t   fired_count;
#endif
  const char        *name;
  shuso_module_event_listener_t *listeners;
}; //shuso_module_event_t

struct shuso_module_context_list_s {
  void **context;
}; //shuso_module_context_list_t

struct shuso_module_state_s {
  shuso_module_t *module;
  void           *state;
}; //shuso_module_state_t


struct shuso_module_context_s {
  shuso_stalloc_t   *stalloc;
  void             **context;
};

bool shuso_add_module(shuso_t *S, shuso_module_t *module);
bool shuso_load_module(shuso_t *S, const char *filename);

shuso_module_t *shuso_current_module(const shuso_t *S);
shuso_module_t *shuso_current_event(const shuso_t *S);

//for module developers:
#define shuso_get_mctx(...) shuso_module_get_context(__VA_ARGS__)
void *shuso_module_get_context(shuso_t *S, const char *modname);

#define shuso_mctx(...) shuso_module_context(__VA_ARGS__)
void *shuso_module_context(shuso_t *S);

#define shuso_parent_mctx(...) shuso_module_parent_get_context(__VA_ARGS__)
void *shuso_module_parent_context(shuso_t *S);

//event stuff
bool shuso_module_event_initialize(shuso_t *S, const char *name, shuso_module_t *mod, shuso_module_event_t *mev);
bool shuso_module_event_listen(shuso_t *S, const char *name, shuso_module_event_fn *callback, void *pd);

const char *shuso_module_event_name(shuso_t *S); //current event name
const char *shuso_module_event_origin(shuso_t *S); //name of module that published current event

// internal stuff
bool shuso_module_system_initialize(shuso_t *S);

#endif //SHUTTLESOCK_MODULE_H
