#ifndef SHUTTLESOCK_MODULE_EVENT_H
#define SHUTTLESOCK_MODULE_EVENT_H

typedef struct {
  bool     cleanup;
  void    *pd;
  void    *d;
} shuso_event_data_type_cleanup_t;

typedef struct {
  const char      *language;
  const char      *data_type;
  bool           (*wrap)(shuso_t *, void *, void **, shuso_event_data_type_cleanup_t *, void *pd);
  bool           (*unwrap)(shuso_t *, void *, void **, shuso_event_data_type_cleanup_t *, void *pd);
  void            *privdata;
} shuso_event_data_type_map_t;

typedef struct {
  shuso_module_t  *module;
  shuso_module_event_fn *fn;
  void            *pd;
  shuso_event_data_type_map_t *wrapper;
  shuso_event_data_type_cleanup_t *wrapper_cleanup;
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  uint8_t          priority;
#endif
} shuso_module_event_listener_t;

struct shuso_module_event_s {
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  size_t             count;
  _Atomic uint64_t   fired_count;
#endif
  const char        *name;
  const char        *data_type;
  bool               firing;
  bool               cancelable;
  shuso_module_event_listener_t *listeners;
}; //shuso_module_event_t

typedef struct {
  const char           *name;
  shuso_module_event_t *event;
  const char           *data_type;
  bool                  cancelable;
} shuso_event_init_t;

struct shuso_event_state_s {
  const shuso_module_t *publisher;
  const shuso_module_t *module;
  const char           *name;
  const char           *data_type;
}; //shuso_event_state_t
//event stuff
void *shuso_events(shuso_t *S, shuso_module_t *module);
bool shuso_events_initialize(shuso_t *S, shuso_module_t *module, shuso_event_init_t *events_init);
bool shuso_event_initialize(shuso_t *S, shuso_module_t *mod, shuso_module_event_t *mev, const char *name, const char *data_type, bool cancelable)

bool shuso_event_listen(shuso_t *S, const char *name, shuso_module_event_fn *callback, void *pd);
bool shuso_event_listen_with_priority(shuso_t *S, const char *name, shuso_module_event_fn *callback, void *pd, int8_t priority);

bool shuso_event_cancel(shuso_t *S, shuso_event_state_t *evstate);

bool shuso_event_publish(shuso_t *S, shuso_module_t *publisher_module, shuso_module_event_t *event, intptr_t code, void *data);
bool shuso_register_event_data_type_mapping(shuso_t *S, shuso_event_data_type_map_t *t, shuso_module_t *registering_module, bool replace_if_present);

bool shuso_event_data_wrap(shuso_t *S, shuso_module_event_t *ev, 

#endif //SHUTTLESOCK_MODULE_EVENT_H
