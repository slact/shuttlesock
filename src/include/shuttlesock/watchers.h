#ifndef SHUTTLESOCK_WATCHERS_H
#define SHUTTLESOCK_WATCHERS_H
#include <shuttlesock/common.h>
#include <ev.h>

struct shuso_ev_io_s {
#ifndef  SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  ev_io          ev;
#else
  union {
    ev_io ev;
    union ev_any_watcher ___any;
  };
  shuso_t       *state;
#endif
}; //shuso_ev_io

struct shuso_ev_timer_s {
#ifndef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  ev_timer       ev;
#else
  union {
    ev_timer ev;
    union ev_any_watcher ___any;
  };
  shuso_t       *state;
#endif
}; //shuso_ev_timer

struct shuso_ev_child_s {
#ifndef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  ev_child       ev;
#else
  union {
    ev_child ev;
    union ev_any_watcher ___any;
  };
  shuso_t       *state;
#endif
}; //shuso_ev_child

struct shuso_ev_signal_s {
#ifndef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  ev_signal      ev;
#else
  union {
    ev_signal ev;
    union ev_any_watcher ___any;
  };
  shuso_t       *state;
#endif
}; //shuso_ev_signal

union shuso_ev_any_u {
  struct shuso_ev_io_s      io;
  struct shuso_ev_timer_s   timer;
  struct shuso_ev_child_s   child;
  struct shuso_ev_signal_s  signal;
  ev_watcher                watcher;
};

#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
#define SHUTTLESOCK_WATCHER_STATE_OFFSET offsetof(shuso_ev_io, state)
_Static_assert(offsetof(union shuso_ev_any_u, watcher) == offsetof(union shuso_ev_any_u, io.ev), "ok...");
_Static_assert(offsetof(shuso_ev_io, state) == offsetof(shuso_ev_io, state) - offsetof(shuso_ev_io, ev), "nonzero shuso_ev_* watcher offset");
_Static_assert(offsetof(shuso_ev_io, state) == offsetof(shuso_ev_timer, state), "mismatching shuso_ev_* state offset");
_Static_assert(offsetof(shuso_ev_io, state) == offsetof(shuso_ev_child, state), "mismatching shuso_ev_* state offset");
_Static_assert(offsetof(shuso_ev_io, state) == offsetof(shuso_ev_signal, state), "mismatching shuso_ev_* state offset");
#endif

#define shuso_ev_active(watcher) ev_is_active(&((watcher)->ev))

shuso_t *shuso_state_from_ev_io(struct ev_loop *loop, shuso_ev_io *w);
shuso_t *shuso_state_from_ev_timer(struct ev_loop *loop, shuso_ev_timer *w);
shuso_t *shuso_state_from_ev_signal(struct ev_loop *loop, shuso_ev_signal *w);
shuso_t *shuso_state_from_ev_child(struct ev_loop *loop, shuso_ev_child *w);
shuso_t *shuso_state_from_raw_ev_watcher(struct ev_loop *loop, ev_watcher *w);
shuso_t *shuso_state_from_raw_ev_io(struct ev_loop *loop, ev_io *w);
shuso_t *shuso_state_from_raw_ev_timer(struct ev_loop *loop, ev_timer *w);
shuso_t *shuso_state_from_raw_ev_signal(struct ev_loop *loop, ev_signal *w);
shuso_t *shuso_state_from_raw_ev_child(struct ev_loop *loop, ev_child *w);
shuso_t *shuso_state_from_raw_ev_dangerously_any(struct ev_loop *loop, void *);

#define shuso_ev_data(watcher) (watcher)->ev.data

typedef void shuso_ev_io_fn(shuso_loop *, shuso_ev_io *, int);
typedef void shuso_ev_timer_fn(shuso_loop *, shuso_ev_timer *, int);
typedef void shuso_ev_child_fn(shuso_loop *, shuso_ev_child *, int);
typedef void shuso_ev_signal_fn(shuso_loop *, shuso_ev_signal *, int);

#define shuso_ev_init(S, watcher, ...) \
  _Generic((watcher), \
           shuso_ev_io *:     shuso_ev_io_init, \
           shuso_ev_timer *:  shuso_ev_timer_init, \
           shuso_ev_signal *: shuso_ev_signal_init, \
           shuso_ev_child *:  shuso_ev_child_init \
  )(S, watcher, __VA_ARGS__)

#define shuso_ev_start(S, watcher) \
  _Generic((watcher), \
           shuso_ev_io *:     shuso_ev_io_start, \
           shuso_ev_timer *:  shuso_ev_timer_start, \
           shuso_ev_signal *: shuso_ev_signal_start, \
           shuso_ev_child *:  shuso_ev_child_start \
  )(S, watcher)

#define shuso_ev_stop(S, watcher) \
  _Generic((watcher), \
           shuso_ev_io *:     shuso_ev_io_stop, \
           shuso_ev_timer *:  shuso_ev_timer_stop, \
           shuso_ev_signal *: shuso_ev_signal_stop, \
           shuso_ev_child *:  shuso_ev_child_stop \
  )(S, watcher)
  
// ev_io
void shuso_ev_io_init(shuso_t *S, shuso_ev_io *w, int fd, int events, shuso_ev_io_fn *cb, void *pd);
void shuso_ev_io_start(shuso_t *S, shuso_ev_io *w);
void shuso_ev_io_stop(shuso_t *S, shuso_ev_io *w);

// ev_timer
void shuso_ev_timer_init(shuso_t *S, shuso_ev_timer *w, ev_tstamp after, ev_tstamp repeat, shuso_ev_timer_fn *cb, void *pd);
void shuso_ev_timer_start(shuso_t *S, shuso_ev_timer *w);
void shuso_ev_timer_again(shuso_t *S, shuso_ev_timer *w);
void shuso_ev_timer_stop(shuso_t *S, shuso_ev_timer *w);

// ev_child
void shuso_ev_child_init(shuso_t *S, shuso_ev_child *w, int pid, int trace, shuso_ev_child_fn *cb, void *pd);
void shuso_ev_child_start(shuso_t *S, shuso_ev_child *w);
void shuso_ev_child_stop(shuso_t *S, shuso_ev_child *w);

// ev_signal
void shuso_ev_signal_init(shuso_t *S, shuso_ev_signal *w, int signal, shuso_ev_signal_fn *cb, void *pd);
void shuso_ev_signal_start(shuso_t *S, shuso_ev_signal *w);
void shuso_ev_signal_stop(shuso_t *S, shuso_ev_signal *w);


shuso_ev_timer *shuso_add_timer_watcher(shuso_t *S, ev_tstamp after, ev_tstamp repeat, shuso_ev_timer_fn *cb, void *pd);
void shuso_remove_timer_watcher(shuso_t *S, shuso_ev_timer *w);

#endif //SHUTTLESOCK_WATCHERS_H
