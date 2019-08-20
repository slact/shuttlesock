#ifndef SHUTTLESOCK_WATCHERS_H
#define SHUTTLESOCK_WATCHERS_H
#include <shuttlesock/common.h>
#include <ev.h>

struct shuso_ev_io_s {
  ev_io          ev;
#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  shuso_t       *ctx;
#endif
}; //shuso_ev_io

struct shuso_ev_timer_s {
  ev_timer       ev;
#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  shuso_t       *ctx;
#endif
}; //shuso_ev_timer

struct shuso_ev_child_s {
  ev_child       ev;
#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  shuso_t       *ctx;
#endif
}; //shuso_ev_child

struct shuso_ev_signal_s {
  ev_signal      ev;
#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  shuso_t       *ctx;
#endif
}; //shuso_ev_signal

#define shuso_ev_active(watcher) (ev_is_active(&(watcher)->ev) || ev_is_pending(&(watcher)->ev))

#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
#define shuso_ev_ctx(loop, watcher) (watcher)->ctx
#else
#define shuso_ev_ctx(loop, watcher) ev_userdata(loop)
#endif

#define shuso_ev_data(watcher) (watcher)->ev.data

// ev_io
void shuso_ev_io_init(shuso_t *ctx, shuso_ev_io *w, int fd, int events, void (*cb)(struct ev_loop *, shuso_ev_io *, int), void *pd);
void shuso_ev_io_start(shuso_t *ctx, shuso_ev_io *w);
void shuso_ev_io_stop(shuso_t *ctx, shuso_ev_io *w);

// ev_timer
void shuso_ev_timer_init(shuso_t *ctx, shuso_ev_timer *w, ev_tstamp after, ev_tstamp repeat, void (*cb)(struct ev_loop *, shuso_ev_timer *, int), void *pd);
void shuso_ev_timer_start(shuso_t *ctx, shuso_ev_timer *w);
void shuso_ev_timer_again(shuso_t *ctx, shuso_ev_timer *w);
void shuso_ev_timer_stop(shuso_t *ctx, shuso_ev_timer *w);

// ev_child
void shuso_ev_child_init(shuso_t *ctx, shuso_ev_child *w, int pid, int trace, void (*cb)(struct ev_loop *, shuso_ev_child *, int), void *pd);
void shuso_ev_child_start(shuso_t *ctx, shuso_ev_child *w);
void shuso_ev_child_stop(shuso_t *ctx, shuso_ev_child *w);

// ev_signal
void shuso_ev_signal_init(shuso_t *ctx, shuso_ev_signal *w, int signal, void (*cb)(struct ev_loop *, shuso_ev_signal *, int), void *pd);
void shuso_ev_signal_start(shuso_t *ctx, shuso_ev_signal *w);
void shuso_ev_signal_stop(shuso_t *ctx, shuso_ev_signal *w);


shuso_ev_timer *shuso_add_timer_watcher(shuso_t *ctx, ev_tstamp after, ev_tstamp repeat, void (*cb)(struct ev_loop *, shuso_ev_timer *, int), void *pd);
void shuso_remove_timer_watcher(shuso_t *ctx, shuso_ev_timer *w);

#endif //SHUTTLESOCK_WATCHERS_H