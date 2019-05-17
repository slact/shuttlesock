#ifndef __SHUTTLESOCK_H
#define __SHUTTLESOCK_H

#include <ev.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <shuttlesock/llist.h>

#define SHUTTLESOCK_MAX_WORKERS 1024

typedef enum {
  SHUSO_ABORT     = 1,
  SHUSO_CONTINUE  = 2,
  SHUSO_DEFER     = 3,
} shuso_nextaction_t;

typedef struct sbuf_s sbuf_t;

struct sbuf_s {
  char           *start;
  char           *cur;
  char           *end;
  sbuf_t         *next;
};
typedef struct {
  sbuf_t         *head;
  sbuf_t         *tail;
} sbuf_list_t;

typedef struct {
  pid_t            id;
  uint16_t         generation;
  struct {       //ipc
    struct {      //pipe
      int             out;
      int             in;
    }               pipe;
    struct {      //fifo
      sbuf_t         *head;
      sbuf_t         *tail;
    }               fifo;
  }               ipc;
} shuso_process_t;

typedef struct shuso_s shuso_t;
typedef bool shuso_callback_fn(shuso_t *ctx, void *pd);
typedef struct {
  shuso_callback_fn *start_master;
  shuso_callback_fn *stop_master;
  shuso_callback_fn *start_manager;
  shuso_callback_fn *stop_manager;
  shuso_callback_fn *start_worker;
  shuso_callback_fn *stop_worker;
  void   *privdata;
} shuso_handlers_t;

typedef struct {
  shuso_handlers_t    handlers;
  shuso_process_t     master;
  shuso_process_t     manager;
  shuso_process_t     worker[SHUTTLESOCK_MAX_WORKERS];
} shuso_shared_t;

#define SHUTTLESOCK_NOPROCESS -3
#define SHUTTLESOCK_MASTER  -2
#define SHUTTLESOCK_MANAGER -1

LLIST_TYPEDEF_LINK_STRUCT(ev_signal);
LLIST_TYPEDEF_LINK_STRUCT(ev_child);
LLIST_TYPEDEF_LINK_STRUCT(ev_io);
LLIST_TYPEDEF_LINK_STRUCT(ev_timer);
LLIST_TYPEDEF_LINK_STRUCT(ev_periodic);

struct shuso_s {
  int                 procnum;
  struct ev_loop     *loop;
  ev_cleanup          loop_cleanup;
  shuso_shared_t *shared;
  struct {      //ipc_fifo
    sbuf_list_t    *list;
    sbuf_list_t    *next;
  }               ipc_fifo;
  struct {     //base_watchers
    LLIST_STRUCT(ev_signal)   signal;
    LLIST_STRUCT(ev_child)    child;
    LLIST_STRUCT(ev_io)       io;
    LLIST_STRUCT(ev_timer)    timer;
    LLIST_STRUCT(ev_periodic) periodic;
  }               base_watchers;
  const char     *errmsg;
}; //shuso_t;

shuso_t *shuso_create(unsigned int ev_loop_flags, shuso_handlers_t *handlers, const char **err);
bool shuso_destroy(shuso_t *ctx);
bool shuso_run(shuso_t *);


ev_signal   *shuso_add_signal_watcher(shuso_t *ctx, void (*cb)(EV_P_ ev_signal *, int), int signum);
ev_child    *shuso_add_child_watcher(shuso_t *ctx, void (*cb)(EV_P_ ev_child *, int), pid_t pid, int trace);
ev_io       *shuso_add_io_watcher(shuso_t *ctx, void (*cb)(EV_P_ ev_io *, int), int fd, int events);
ev_timer    *shuso_add_timer_watcher(shuso_t *ctx, void (*cb)(EV_P_ ev_timer *, int), ev_tstamp after, ev_tstamp repeat);
ev_periodic *shuso_add_periodic_watcher(shuso_t *ctx, void (*cb)(EV_P_ ev_periodic *, int), ev_tstamp offset, ev_tstamp interval, ev_tstamp (*reschedule_cb)(ev_periodic *, ev_tstamp));

void shuso_remove_signal_watcher(shuso_t *ctx, ev_signal *w);
void shuso_remove_child_watcher(shuso_t *ctx, ev_child *w);
void shuso_remove_io_watcher(shuso_t *ctx, ev_io *w);
void shuso_remove_timer_watcher(shuso_t *ctx, ev_timer *w);
void shuso_remove_periodic_watcher(shuso_t *ctx, ev_periodic *w);
  
#endif //__SHUTTLESOCK_H
