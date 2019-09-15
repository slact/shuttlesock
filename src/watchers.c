#include <shuttlesock.h>
#include <assert.h>

//ev_io

typedef void ev_io_fn(struct ev_loop *, ev_io *, int);

void shuso_ev_io_init(shuso_t *S, shuso_ev_io *w, int fd, int events, shuso_ev_io_fn *cb, void *pd) {
#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  w->state = S;
#endif
  w->ev.data = pd;
  ev_io_init(&w->ev, (ev_io_fn *)cb, fd, events);
}
void shuso_ev_io_start(shuso_t *S, shuso_ev_io *w) {
  ev_io_start(S->ev.loop, &w->ev);
}
void shuso_ev_io_stop(shuso_t *S, shuso_ev_io *w) {
  ev_io_stop(S->ev.loop, &w->ev);
}


// ev_timer

typedef void ev_timer_fn(struct ev_loop *, ev_timer *, int);

void shuso_ev_timer_init(shuso_t *S, shuso_ev_timer *w, ev_tstamp after, ev_tstamp repeat, shuso_ev_timer_fn *cb, void *pd) {
#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  w->state = S;
#endif
  w->ev.data = pd;
  ev_timer_init(&w->ev, (ev_timer_fn *)cb, after, repeat);
}

void shuso_ev_timer_start(shuso_t *S, shuso_ev_timer *w) {
  ev_timer_start(S->ev.loop, &w->ev);
}
void shuso_ev_timer_again(shuso_t *S, shuso_ev_timer *w) {
  ev_timer_again(S->ev.loop, &w->ev);
}
void shuso_ev_timer_stop(shuso_t *S, shuso_ev_timer *w) {
  ev_timer_stop(S->ev.loop, &w->ev);
}


//ev_child

typedef void ev_child_fn(struct ev_loop *, ev_child *, int);

void shuso_ev_child_init(shuso_t *S, shuso_ev_child *w, int pid, int trace, shuso_ev_child_fn *cb, void *pd) {
#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  w->state = S;
#endif
  w->ev.data = pd;
  ev_child_init(&w->ev, (ev_child_fn *)cb, pid, trace);
}

void shuso_ev_child_start(shuso_t *S, shuso_ev_child *w) {
  ev_child_start(S->ev.loop, &w->ev);
}
void shuso_ev_child_stop(shuso_t *S, shuso_ev_child *w) {
  ev_child_stop(S->ev.loop, &w->ev);
}

//ev_signal

typedef void ev_signal_fn(struct ev_loop *, ev_signal *, int);

void shuso_ev_signal_init(shuso_t *S, shuso_ev_signal *w, int signal, shuso_ev_signal_fn *cb, void *pd) {
#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
  w->state = S;
#endif
  w->ev.data = pd;
  ev_signal_init(&w->ev, (ev_signal_fn *)cb, signal);
}

void shuso_ev_signal_start(shuso_t *S, shuso_ev_signal *w) {
  //shuso_log_debug(S, "%d", w->ev.signum);
  ev_signal_start(S->ev.loop, &w->ev);
}
void shuso_ev_signal_stop(shuso_t *S, shuso_ev_signal *w) {
  ev_signal_stop(S->ev.loop, &w->ev);
}


#ifdef SHUTTLESOCK_DEBUG_NO_WORKER_THREADS
shuso_t *shuso_state_from_ev_io(struct ev_loop *loop, shuso_ev_io *w) {
  return w->state;
}
shuso_t *shuso_state_from_ev_timer(struct ev_loop *loop, shuso_ev_timer *w) {
  return w->state;
}
shuso_t *shuso_state_from_ev_signal(struct ev_loop *loop, shuso_ev_signal *w) {
  return w->state;
}
shuso_t *shuso_state_from_ev_child(struct ev_loop *loop, shuso_ev_child *w) {
  return w->state;
}
shuso_t *shuso_state_from_raw_ev_watcher(struct ev_loop *loop, ev_watcher *w) {
  return (shuso_t *)(&w[1]); //struct-aligning magic at work. it's ugly but it works
}
#else
shuso_t *shuso_state_from_ev_io(struct ev_loop *loop, shuso_ev_io *w) {
  return ev_userdata(loop);
}
shuso_t *shuso_state_from_ev_timer(struct ev_loop *loop, shuso_ev_timer *w) {
  return ev_userdata(loop);
}
shuso_t *shuso_state_from_ev_signal(struct ev_loop *loop, shuso_ev_signal *w) {
  return ev_userdata(loop);
}
shuso_t *shuso_state_from_ev_child(struct ev_loop *loop, shuso_ev_child *w) {
  return ev_userdata(loop);
}
shuso_t *shuso_state_from_raw_ev_watcher(struct ev_loop *loop, ev_watcher *w) {
  return ev_userdata(loop);
}
#endif
shuso_t *shuso_state_from_raw_ev_io(struct ev_loop *loop, ev_io *w) {
  return shuso_state_from_raw_ev_watcher(loop, (ev_watcher *)w);
}
shuso_t *shuso_state_from_raw_ev_timer(struct ev_loop *loop, ev_timer *w) {
  return shuso_state_from_raw_ev_watcher(loop, (ev_watcher *)w);
}
shuso_t *shuso_state_from_raw_ev_signal(struct ev_loop *loop, ev_signal *w) {
  return shuso_state_from_raw_ev_watcher(loop, (ev_watcher *)w);
}
shuso_t *shuso_state_from_raw_ev_child(struct ev_loop *loop, ev_child *w) {
  return shuso_state_from_raw_ev_watcher(loop, (ev_watcher *)w);
}
shuso_t *shuso_state_from_raw_ev_dangerously_any(struct ev_loop *loop, void *w){
  //this will work on shuso_ev_* events, and may crash and burn for anything else
  return shuso_state_from_raw_ev_watcher(loop, (ev_watcher *)w);
}

shuso_ev_timer *shuso_add_timer_watcher(shuso_t *S, ev_tstamp after, ev_tstamp repeat, shuso_ev_timer_fn *cb, void *pd) {
  shuso_ev_timer_link_t  *wl = malloc(sizeof(*wl));
  shuso_ev_timer         *w;
  if(wl == NULL) return NULL;
  w = &wl->data;
  shuso_ev_timer_init(S, w, after, repeat, cb, pd);
  shuso_ev_timer_start(S, w);
  llist_append(S->base_watchers.timer, wl);
  return w;
}
void shuso_remove_timer_watcher(shuso_t *S, shuso_ev_timer *w) {
  shuso_ev_timer_link_t *wl = llist_link(w, shuso_ev_timer);
  shuso_ev_timer_stop(S, w);
  llist_remove(S->base_watchers.timer, wl);
  free(wl);
}
