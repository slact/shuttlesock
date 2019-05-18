#include <stdlib.h>
#include <shuttlesock.h>
#include <ev.h>
#include <assert.h>
#include "shuttlesock_private.h"

//ugly repetition follows

ev_signal *shuso_add_signal_watcher(shuso_t *ctx, void (*cb)(EV_P_ ev_signal *, int), void *pd, int signum) {
  ev_signal_link_t  *wl = malloc(sizeof(*wl));
  ev_signal         *w;
  if(wl == NULL) return NULL;
  w = &wl->data;
  ev_signal_init(w, cb, signum);
  w->data = pd;
  ev_signal_start(ctx->loop, w);
  llist_append(ctx->base_watchers.signal, wl);
  return w;
}
void shuso_remove_signal_watcher(shuso_t *ctx, ev_signal *w) {
  ev_signal_link_t *wl = llist_link(w, ev_signal);
  ev_signal_stop(ctx->loop, w);
  llist_remove(ctx->base_watchers.signal, wl);
  free(wl);
}

ev_child *shuso_add_child_watcher(shuso_t *ctx, void (*cb)(EV_P_ ev_child *, int), void *pd, pid_t pid, int trace) {
  ev_child_link_t  *wl = malloc(sizeof(*wl));
  ev_child         *w;
  if(wl == NULL) return NULL;
  w = &wl->data;
  ev_child_init(w, cb, pid, trace);
  w->data = pd;
  ev_child_start(ctx->loop, w);
  llist_append(ctx->base_watchers.child, wl);
  return w;
}
void shuso_remove_child_watcher(shuso_t *ctx, ev_child *w) {
  ev_child_link_t *wl = llist_link(w, ev_child);
  ev_child_stop(ctx->loop, w);
  llist_remove(ctx->base_watchers.child, wl);
  free(wl);
}

ev_io *shuso_add_io_watcher(shuso_t *ctx, void (*cb)(EV_P_ ev_io *, int), void *pd, int fd, int events) {
  ev_io_link_t  *wl = malloc(sizeof(*wl));
  ev_io         *w;
  if(wl == NULL) return NULL;
  w = &wl->data;
  ev_io_init(w, cb, fd, events);
  w->data = pd;
  ev_io_start(ctx->loop, w);
  llist_append(ctx->base_watchers.io, wl);
  return w;
}
void shuso_remove_io_watcher(shuso_t *ctx, ev_io *w) {
  ev_io_link_t *wl = llist_link(w, ev_io);
  ev_io_stop(ctx->loop, w);
  llist_remove(ctx->base_watchers.io, wl);
  free(wl);
}

ev_timer *shuso_add_timer_watcher(shuso_t *ctx, void (*cb)(EV_P_ ev_timer *, int), void *pd, ev_tstamp after, ev_tstamp repeat) {
  ev_timer_link_t  *wl = malloc(sizeof(*wl));
  ev_timer         *w;
  if(wl == NULL) return NULL;
  w = &wl->data;
  ev_timer_init(w, cb, after, repeat);
  w->data = pd;
  ev_timer_start(ctx->loop, w);
  llist_append(ctx->base_watchers.timer, wl);
  return w;
}
void shuso_remove_timer_watcher(shuso_t *ctx, ev_timer *w) {
  ev_timer_link_t *wl = llist_link(w, ev_timer);
  ev_timer_stop(ctx->loop, w);
  llist_remove(ctx->base_watchers.timer, wl);
  free(wl);
}

ev_periodic *shuso_add_periodic_watcher(shuso_t *ctx, void (*cb)(EV_P_ ev_periodic *, int), void *pd, ev_tstamp offset, ev_tstamp interval, ev_tstamp (*reschedule_cb)(ev_periodic *w, ev_tstamp now)) {
  ev_periodic_link_t  *wl = malloc(sizeof(*wl));
  ev_periodic         *w;
  if(wl == NULL) return NULL;
  w = &wl->data;
  ev_periodic_init(w, cb, offset, interval, reschedule_cb);
  w->data = pd;
  ev_periodic_start(ctx->loop, w);
  llist_append(ctx->base_watchers.periodic, wl);
  return w;
}
void shuso_remove_periodic_watcher(shuso_t *ctx, ev_periodic *w) {
  ev_periodic_link_t *wl = llist_link(w, ev_periodic);
  ev_periodic_stop(ctx->loop, w);
  llist_remove(ctx->base_watchers.periodic, wl);
  free(wl);
}
