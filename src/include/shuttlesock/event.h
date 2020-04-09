#ifndef SHUTTLESOCK_EVENT_H
#define SHUTTLESOCK_EVENT_H

typedef struct {
  shuso_module_t  *module;
  shuso_event_fn  *fn;
  void            *pd;
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  uint8_t          priority;
#endif
} shuso_event_listener_t;

typedef enum {
  SHUSO_EVENT_NO_INTERRUPT = 0,
  SHUSO_EVENT_PAUSE,
  SHUSO_EVENT_CANCEL,
  SHUSO_EVENT_DELAY
} shuso_event_interrupt_t;

typedef bool shuso_event_interrupt_handler_fn(shuso_t *S, shuso_event_t *event, shuso_event_state_t *evstate, shuso_event_interrupt_t interrupt, double *sec);

typedef struct shuso_event_s {
  const char        *name;
  const char        *data_type;
  shuso_event_listener_t *listeners;
  shuso_event_interrupt_handler_fn *interrupt_handler;
  uint16_t           module_index;
  unsigned           detached:1;
  unsigned           firing:1;
#ifdef SHUTTLESOCK_DEBUG_MODULE_SYSTEM
  shuso_event_interrupt_t interrupt_state;
  size_t             count;
  _Atomic uint64_t   fired_count;
#endif
} shuso_event_t;

typedef struct shuso_event_pause_s {
  const char           *reason;
  shuso_event_t        *event;
  intptr_t              code;
  void                 *data;
  uint16_t             next_listener_index;
} shuso_event_pause_t;

typedef struct shuso_event_delay_s {
  shuso_event_pause_t paused;
  shuso_ev_timer        timer;
  lua_reference_t       ref;
} shuso_event_delay_t;

typedef struct {
  const char           *name;
  shuso_event_t        *event;
  const char           *data_type;
  shuso_event_interrupt_handler_fn *interrupt_handler;
  bool                  detached;
} shuso_event_init_t;

typedef struct shuso_event_state_s {
  const shuso_module_t *publisher;
  const shuso_module_t *module;
  const char           *name;
  const char           *data_type;
} shuso_event_state_t;

//event stuff
bool shuso_events_initialize(shuso_t *S, shuso_module_t *module, shuso_event_init_t *events_init);
bool shuso_event_initialize(shuso_t *S, shuso_module_t *mod, shuso_event_t *mev, shuso_event_init_t *event_init);

#define shuso_event_listen(S, evt, callback, pd) \
  _Generic((evt), \
    const shuso_event_t*:shuso_event_by_pointer_listen_with_priority, \
    shuso_event_t  *    :shuso_event_by_pointer_listen_with_priority, \
    char *              :shuso_event_by_name_listen_with_priority, \
    const char *        :shuso_event_by_name_listen_with_priority \
  )(S, evt, callback, pd, 0)

#define shuso_event_listen_with_priority(S, evt, callback, pd, priority) \
  _Generic((evt), \
    const shuso_event_t*:shuso_event_by_pointer_listen_with_priority, \
    shuso_event_t  *    :shuso_event_by_pointer_listen_with_priority, \
    char *              :shuso_event_by_name_listen_with_priority, \
    const char *        :shuso_event_by_name_listen_with_priority \
  )(S, evt, callback, pd, priority)

bool shuso_event_by_name_listen_with_priority(shuso_t *S, const char *name, shuso_event_fn *callback, void *pd, int8_t priority);
bool shuso_event_by_pointer_listen_with_priority(shuso_t *S, shuso_event_t *event, shuso_event_fn *callback, void *pd, int8_t priority);
  

bool shuso_event_cancel(shuso_t *S, shuso_event_state_t *evstate);

bool shuso_event_pause(shuso_t *S, shuso_event_state_t *evstate, const char *reason,  shuso_event_pause_t *paused);
bool shuso_event_delay(shuso_t *S, shuso_event_state_t *evstate, const char *reason, double max_delay_sec, int *delay_ref);
#define shuso_event_resume(S, resume_data) \
  _Generic((resume_data), \
    int                  :shuso_event_resume_delayed, \
    shuso_event_pause_t *:shuso_event_resume_paused \
  )(S, resume_data)

bool shuso_event_resume_delayed(shuso_t *S, int delay_id);
bool shuso_event_resume_paused(shuso_t *S, shuso_event_pause_t *paused);
  
bool shuso_event_publish(shuso_t *S, shuso_event_t *event, intptr_t code, void *data);

#endif //SHUTTLESOCK_EVENT_H
