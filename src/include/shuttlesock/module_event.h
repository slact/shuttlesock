#ifndef SHUTTLESOCK_MODULE_EVENT_H
#define SHUTTLESOCK_MODULE_EVENT_H

typedef struct {
  shuso_module_t  *module;
  shuso_module_event_fn *fn;
  void            *pd;
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
bool shuso_events_initialize(shuso_t *S, shuso_module_t *module, shuso_event_init_t *events_init);
bool shuso_event_initialize(shuso_t *S, shuso_module_t *mod, shuso_module_event_t *mev, const char *name, const char *data_type, bool cancelable);

bool shuso_event_listen(shuso_t *S, const char *name, shuso_module_event_fn *callback, void *pd);
bool shuso_event_listen_with_priority(shuso_t *S, const char *name, shuso_module_event_fn *callback, void *pd, int8_t priority);

bool shuso_event_cancel(shuso_t *S, shuso_event_state_t *evstate);

bool shuso_event_publish(shuso_t *S, shuso_module_t *publisher_module, shuso_module_event_t *event, intptr_t code, void *data);

#endif //SHUTTLESOCK_MODULE_EVENT_H
