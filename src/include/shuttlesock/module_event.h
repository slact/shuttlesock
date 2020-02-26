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

typedef struct shuso_module_event_s {
  const char        *name;
  const char        *data_type;
  shuso_module_event_listener_t *listeners;
  uint16_t           module_index;
  unsigned           firing:1;
  unsigned           cancelable:1;
  unsigned           pausable:1;
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  size_t             count;
  _Atomic uint64_t   fired_count;
#endif
} shuso_module_event_t;

typedef struct shuso_module_paused_event_s {
  shuso_module_event_t *event;
  intptr_t              code;
  void                 *data;
  uint16_t             next_listener_index;
} shuso_module_paused_event_t;

typedef struct {
  const char           *name;
  shuso_module_event_t *event;
  const char           *data_type;
  bool                  cancelable;
  bool                  pausable;
} shuso_event_init_t;

typedef struct shuso_event_state_s {
  const shuso_module_t *publisher;
  const shuso_module_t *module;
  const char           *name;
  const char           *data_type;
} shuso_event_state_t;

//event stuff
bool shuso_events_initialize(shuso_t *S, shuso_module_t *module, shuso_event_init_t *events_init);
bool shuso_event_initialize(shuso_t *S, shuso_module_t *mod, shuso_module_event_t *mev, shuso_event_init_t *event_init);

bool shuso_event_listen(shuso_t *S, const char *name, shuso_module_event_fn *callback, void *pd);
bool shuso_event_listen_with_priority(shuso_t *S, const char *name, shuso_module_event_fn *callback, void *pd, int8_t priority);

bool shuso_event_cancel(shuso_t *S, shuso_event_state_t *evstate);

bool shuso_event_pause(shuso_t *S, shuso_event_state_t *evstate, shuso_module_paused_event_t *paused);
bool shuso_event_resume(shuso_t *S, shuso_module_paused_event_t *paused);

bool shuso_event_publish(shuso_t *S, shuso_module_event_t *event, intptr_t code, void *data);

#endif //SHUTTLESOCK_MODULE_EVENT_H
