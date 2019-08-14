#include <stdlib.h>
#include <shuttlesock.h>
#include <ev.h>
#include <assert.h>
#include <shuttlesock/log.h>
#include "shuttlesock_private.h"

//ev_io

typedef void (*ev_io_fn)(struct ev_loop *, ev_io *, int);

void shuso_ev_io_init(shuso_t *ctx, shuso_ev_io *w, int fd, int events, void (*cb)(struct ev_loop *, shuso_ev_io *, int), void *pd) {
#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  w->ctx = ctx;
#endif
  w->ev.data = pd;
  ev_io_init(&w->ev, (ev_io_fn )cb, fd, events);
}
void shuso_ev_io_start(shuso_t *ctx, shuso_ev_io *w) {
  ev_io_start(ctx->ev.loop, &w->ev);
}
void shuso_ev_io_stop(shuso_t *ctx, shuso_ev_io *w) {
  ev_io_stop(ctx->ev.loop, &w->ev);
}


// ev_timer

typedef void (*ev_timer_fn)(struct ev_loop *, ev_timer *, int);

void shuso_ev_timer_init(shuso_t *ctx, shuso_ev_timer *w, ev_tstamp after, ev_tstamp repeat, void (*cb)(struct ev_loop *, shuso_ev_timer *, int), void *pd) {
#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  w->ctx = ctx;
#endif
  w->ev.data = pd;
  ev_timer_init(&w->ev, (ev_timer_fn )cb, after, repeat);
}

void shuso_ev_timer_start(shuso_t *ctx, shuso_ev_timer *w) {
  ev_timer_start(ctx->ev.loop, &w->ev);
}
void shuso_ev_timer_again(shuso_t *ctx, shuso_ev_timer *w) {
  ev_timer_again(ctx->ev.loop, &w->ev);
}
void shuso_ev_timer_stop(shuso_t *ctx, shuso_ev_timer *w) {
  ev_timer_stop(ctx->ev.loop, &w->ev);
}


//ev_child

typedef void (*ev_child_fn)(struct ev_loop *, ev_child *, int);

void shuso_ev_child_init(shuso_t *ctx, shuso_ev_child *w, int pid, int trace, void (*cb)(struct ev_loop *, shuso_ev_child *, int), void *pd) {
#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  w->ctx = ctx;
#endif
  w->ev.data = pd;
  ev_child_init(&w->ev, (ev_child_fn )cb, pid, trace);
}

void shuso_ev_child_start(shuso_t *ctx, shuso_ev_child *w) {
  ev_child_start(ctx->ev.loop, &w->ev);
}
void shuso_ev_child_stop(shuso_t *ctx, shuso_ev_child *w) {
  ev_child_stop(ctx->ev.loop, &w->ev);
}

//ev_signal

typedef void (*ev_signal_fn)(struct ev_loop *, ev_signal *, int);

void shuso_ev_signal_init(shuso_t *ctx, shuso_ev_signal *w, int signal, void (*cb)(struct ev_loop *, shuso_ev_signal *, int), void *pd) {
#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  w->ctx = ctx;
#endif
  w->ev.data = pd;
  ev_signal_init(&w->ev, (ev_signal_fn )cb, signal);
}

void shuso_ev_signal_start(shuso_t *ctx, shuso_ev_signal *w) {
  ev_signal_start(ctx->ev.loop, &w->ev);
}
void shuso_ev_signal_stop(shuso_t *ctx, shuso_ev_signal *w) {
  ev_signal_stop(ctx->ev.loop, &w->ev);
}



shuso_ev_timer *shuso_add_timer_watcher(shuso_t *ctx, ev_tstamp after, ev_tstamp repeat, void (*cb)(struct ev_loop *, shuso_ev_timer *, int), void *pd) {
  shuso_ev_timer_link_t  *wl = malloc(sizeof(*wl));
  shuso_ev_timer         *w;
  if(wl == NULL) return NULL;
  w = &wl->data;
  shuso_ev_timer_init(ctx, w, after, repeat, cb, pd);
  shuso_ev_timer_start(ctx, w);
  llist_append(ctx->base_watchers.timer, wl);
  return w;
}
void shuso_remove_timer_watcher(shuso_t *ctx, shuso_ev_timer *w) {
  shuso_ev_timer_link_t *wl = llist_link(w, shuso_ev_timer);
  shuso_ev_timer_stop(ctx, w);
  llist_remove(ctx->base_watchers.timer, wl);
  free(wl);
}
