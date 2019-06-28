#ifndef SHUTTLESOCK_H
#define SHUTTLESOCK_H

#include <ev.h>
#include <pthread.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <shuttlesock/configure.h>
#include <shuttlesock/sbuf.h>
#include <shuttlesock/llist.h>
#include <shuttlesock/ipc.h>
#include <shuttlesock/stalloc.h>
#include <shuttlesock/resolver.h>


#define SHUTTLESOCK_MAX_WORKERS 1024

typedef enum {
  SHUSO_ABORT     = 0,
  SHUSO_FINISH    = 1,
  SHUSO_CONTINUE  = 2,
  SHUSO_DEFER     = 3,
} shuso_nextaction_t;

typedef enum {
  //non-positive states MUST be kinds of non-running states
  SHUSO_PROCESS_STATE_DEAD = -1,
  SHUSO_PROCESS_STATE_NIL = 0,
  //positive states MUST be kinds of running states
  SHUSO_PROCESS_STATE_STARTING = 1,
  SHUSO_PROCESS_STATE_RUNNING  = 2,
  SHUSO_PROCESS_STATE_STOPPING = 3
} shuso_process_state_t;



typedef struct shuso_process_s {
  pid_t                             pid;
  pthread_t                         tid;
  _Atomic(shuso_process_state_t)   *state;
  uint16_t                          generation;
  shuso_ipc_channel_shared_t        ipc;
} shuso_process_t;

typedef struct shuso_s shuso_t;
typedef void shuso_io_cb_fn(EV_P_ ev_io *, int);
typedef void shuso_cb_fn(shuso_t *ctx, void *pd);
typedef struct {
  shuso_cb_fn *start_master;
  shuso_cb_fn *stop_master;
  shuso_cb_fn *start_manager;
  shuso_cb_fn *stop_manager;
  shuso_cb_fn *start_worker;
  shuso_cb_fn *stop_worker;
  void   *privdata;
} shuso_handlers_t;

#define SHUTTLESOCK_CONFIG_DEFAULT_IPC_BUFFER_SIZE  32
#define SHUTTLESOCK_CONFIG_DEFAULT_IPC_SEND_RETRY_DELAY  0.050
#define SHUTTLESOCK_CONFIG_DEFAULT_IPC_SEND_TIMEOUT 0.500

typedef struct {
  const char        *name;
  union {
    struct in6_addr addr6;
    struct in_addr  addr;
    const char     *path;
  };
  uint16_t          addr_family; //address family: AF_INET/AF_INET6/AF_UNIX
  uint16_t          port; //CPU-native port
  int               fd;
  unsigned          udp:1; //TCP or UDP?
} shuso_host_t;


//the shuso_config struct is designed to be zeroed on initialization
typedef struct shuso_config_s {
  struct {          //ipc
    float               send_retry_delay;
    float               send_timeout;
  }                   ipc;
  struct {          //features
    int                 io_uring;
  }                   features;
  struct {          //resolver
    int                 timeout; //milliseconds
    int                 tries;
    shuso_host_t       *hosts;
    off_t               hosts_count;
  }                   resolver;
  int                 workers;
} shuso_config_t;

typedef struct {
  shuso_handlers_t    phase_handlers;
  shuso_ipc_handler_t ipc_handlers[256];
  shuso_config_t      config;
  struct {          //process
    shuso_process_t     master;
    shuso_process_t     manager;
    shuso_process_t     worker[SHUTTLESOCK_MAX_WORKERS];
    uint16_t            workers_start;
    uint16_t            workers_end;
  }                   process;
  struct {          //log
    int                 fd;
  }                   log;
  struct {          //features
#ifdef SHUTTLESOCK_USE_IO_URING
    bool                io_uring;
#endif
  }                   features;
  struct {          //shm
    void               *ptr;
    size_t              sz;
  }                   shm;
} shuso_common_t;

#define SHUTTLESOCK_NOPROCESS  -3
#define SHUTTLESOCK_MASTER     -2
#define SHUTTLESOCK_MANAGER    -1
#define SHUTTLESOCK_WORKER      0

LLIST_TYPEDEF_LINK_STRUCT(ev_signal);
LLIST_TYPEDEF_LINK_STRUCT(ev_child);
LLIST_TYPEDEF_LINK_STRUCT(ev_io);
LLIST_TYPEDEF_LINK_STRUCT(ev_timer);
LLIST_TYPEDEF_LINK_STRUCT(ev_periodic);

struct shuso_s {
  int                         procnum;
  shuso_process_t            *process;
  shuso_ipc_channel_local_t   ipc;
  struct {                  //ev
    struct ev_loop             *loop;
    unsigned int                flags;
  }                           ev;
  
  shuso_common_t             *common;
  struct {                  //base_watchers
    LLIST_STRUCT(ev_signal)     signal;
    LLIST_STRUCT(ev_child)      child;
    LLIST_STRUCT(ev_io)         io;
    LLIST_STRUCT(ev_timer)      timer;
    LLIST_STRUCT(ev_periodic)   periodic;
  }                           base_watchers;
  shuso_stalloc_t             stalloc;
  shuso_resolver_t            resolver;
  void                       *data;  //custom data attached to this shuttlesock context
  const char                 *errmsg;
}; //shuso_t;

typedef enum {
  SHUSO_STOP_ASK =      1,
  SHUSO_STOP_INSIST =   2,
  SHUSO_STOP_DEMAND =   3,
  SHUSO_STOP_COMMAND =  4,
  SHUSO_STOP_FORCE =    5
} shuso_stop_t;

shuso_t *shuso_create(unsigned int ev_loop_flags, shuso_handlers_t *handlers, shuso_config_t *config, const char **err);
bool shuso_destroy(shuso_t *ctx);
bool shuso_run(shuso_t *);
bool shuso_stop(shuso_t *ctx, shuso_stop_t forcefulness);
bool shuso_spawn_manager(shuso_t *ctx);
bool shuso_stop_manager(shuso_t *ctx, shuso_stop_t forcefulness);
bool shuso_spawn_worker(shuso_t *ctx, shuso_process_t *proc);
bool shuso_stop_worker(shuso_t *ctx, shuso_process_t *proc, shuso_stop_t
 forcefulness);
bool shuso_stop_manager(shuso_t *ctx, shuso_stop_t forcefulness);

bool shuso_is_master(shuso_t *ctx);
bool shuso_is_forked_manager(shuso_t *ctx);

bool shuso_set_log_fd(shuso_t *ctx, int fd);

bool shuso_set_error(shuso_t *ctx, const char *err);
shuso_process_t *shuso_procnum_to_process(shuso_t *ctx, int procnum);
int shuso_process_to_procnum(shuso_t *ctx, shuso_process_t *proc);


#define SHUSO_EACH_WORKER(ctx, cur) \
  for(shuso_process_t *cur = &ctx->common->process.worker[ctx->common->process.workers_start], *___worker_end = &ctx->common->process.worker[ctx->common->process.workers_end]; cur < ___worker_end; cur++)
#define shuso_set_nonblocking(fd) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK)
ev_signal   *shuso_add_signal_watcher(shuso_t *ctx, void (*cb)(EV_P_ ev_signal *, int), void *pd, int signum);
ev_child    *shuso_add_child_watcher(shuso_t *ctx, void (*cb)(EV_P_ ev_child *, int), void *pd, pid_t pid, int trace);
ev_io       *shuso_add_io_watcher(shuso_t *ctx, void (*cb)(EV_P_ ev_io *, int), void *pd, int fd, int events);
ev_timer    *shuso_add_timer_watcher(shuso_t *ctx, void (*cb)(EV_P_ ev_timer *, int), void *pd, ev_tstamp after, ev_tstamp repeat);
ev_periodic *shuso_add_periodic_watcher(shuso_t *ctx, void (*cb)(EV_P_ ev_periodic *, int), void *pd, ev_tstamp offset, ev_tstamp interval, ev_tstamp (*reschedule_cb)(ev_periodic *, ev_tstamp));

void shuso_remove_signal_watcher(shuso_t *ctx, ev_signal *w);
void shuso_remove_child_watcher(shuso_t *ctx, ev_child *w);
void shuso_remove_io_watcher(shuso_t *ctx, ev_io *w);
void shuso_remove_timer_watcher(shuso_t *ctx, ev_timer *w);
void shuso_remove_periodic_watcher(shuso_t *ctx, ev_periodic *w);
  
#endif //SHUTTLESOCK_H
