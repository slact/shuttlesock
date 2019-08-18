#ifndef SHUTTLESOCK_H
#define SHUTTLESOCK_H

#include <ev.h>
#include <pthread.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <netinet/in.h>
#include <shuttlesock/configure.h>
#include <shuttlesock/watchers.h>
#include <shuttlesock/common.h>
#include <shuttlesock/sbuf.h>
#include <shuttlesock/llist.h>
#include <shuttlesock/ipc.h>
#include <shuttlesock/stalloc.h>
#include <shuttlesock/resolver.h>
#include <shuttlesock/shared_slab.h>


struct shuso_process_s {
  pid_t                             pid;
  pthread_t                         tid;
  _Atomic(shuso_process_state_t)   *state;
  uint16_t                          generation;
  shuso_ipc_channel_shared_t        ipc;
};

//params for setsockopt()
struct shuso_sockopt_s {
  int           level;
  int           name;
  int           intvalue;
}; //shuso_sockopt_t

struct shuso_sockopts_s {
  size_t           count;
  shuso_sockopt_t *array;
}; //shuso_sockopts_t

struct shuso_hostinfo_s {
  const char        *name;
  union {
    struct in6_addr addr6;
    struct in_addr  addr;
    const char     *path;
  };
  uint16_t          addr_family; //address family: AF_INET/AF_INET6/AF_UNIX
  uint16_t          port; //CPU-native port
  unsigned          udp:1; //TCP or UDP?
}; //shuso_hostinfo_t

struct shuso_socket_s {
  shuso_hostinfo_t  host;
  int               fd;
  shuso_socket_fn  *handler;
  shuso_socket_fn  *cheanup;
  void              *data;
}; //shuso_socket_t;

struct shuso_handlers_s {
  shuso_handler_fn *start_master;
  shuso_handler_fn *stop_master;
  shuso_handler_fn *start_manager;
  shuso_handler_fn *stop_manager;
  shuso_handler_fn *start_worker;
  shuso_handler_fn *stop_worker;
  void   *privdata;
}; //shuso_handlers_t

//the shuso_config struct is designed to be zeroed on initialization
struct shuso_config_s {
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
    struct {
      off_t               count;
      shuso_hostinfo_t   *array;
    }                   hosts;
  }                   resolver;
  size_t              shared_slab_size;
  const char         *username;
  const char         *groupname;
  uid_t               uid;
  gid_t               gid;
  int                 workers;
}; // shuso_config_t

struct shuso_common_s {
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
    bool                io_uring;
  }                   features;
  shuso_shared_slab_t shm;
}; //shuso_common_t

LLIST_TYPEDEF_LINK_STRUCT(shuso_ev_timer);

struct shuso_s {
  int                         procnum;
  shuso_process_t            *process;
  shuso_ipc_channel_local_t   ipc;
  struct {                  //ev
    shuso_loop                 *loop;
    unsigned int                flags;
  }                           ev;
  
  shuso_common_t             *common;
  struct {                  //base_watchers
    shuso_ev_signal              signal[8];
    shuso_ev_child               child;
    LLIST_STRUCT(shuso_ev_timer) timer;
  }                           base_watchers;
  shuso_stalloc_t             stalloc;
  shuso_shared_slab_t         shm;
  shuso_resolver_t            resolver;
  void                       *data;  //custom data attached to this shuttlesock context
  const char                 *errmsg;
  char                        logbuf[1024];
}; //shuso_t;

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


void shuso_listen(shuso_t *ctx, shuso_hostinfo_t *bind, shuso_handler_fn handler, shuso_handler_fn cleanup, void *pd);
  
#endif //SHUTTLESOCK_H
