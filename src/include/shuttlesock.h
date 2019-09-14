#ifndef SHUTTLESOCK_H
#define SHUTTLESOCK_H

#include <lua.h>
#include <ev.h>
#include <pthread.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <shuttlesock/build_config.h>
#include <shuttlesock/watchers.h>
#include <shuttlesock/common.h>
#include <shuttlesock/sbuf.h>
#include <shuttlesock/llist.h>
#include <shuttlesock/ipc.h>
#include <shuttlesock/stalloc.h>
#include <shuttlesock/resolver.h>
#include <shuttlesock/shared_slab.h>
#include <shuttlesock/lua_bridge.h>
#include <shuttlesock/sysutil.h>
#include <shuttlesock/log.h>
#include <shuttlesock/config_file.h>


struct shuso_process_s {
  pid_t                             pid;
  pthread_t                         tid;
  _Atomic(shuso_process_state_t)   *state;
  uint16_t                          generation;
  shuso_ipc_channel_shared_t        ipc;
}; // shuso_process_t

//params for setsockopt()
struct shuso_sockopt_s {
  int           level;
  int           name;
  union {
    int           integer;
    int           flag;
    struct timeval timeval;
    struct linger linger;
  }             value;
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

struct shuso_runtime_handlers_s {
  shuso_handler_fn *start_master;
  shuso_handler_fn *stop_master;
  shuso_handler_fn *start_manager;
  shuso_handler_fn *stop_manager;
  shuso_handler_fn *start_worker;
  shuso_handler_fn *stop_worker;
  void   *privdata;
}; //shuso_runtime_handlers_t

struct shuso_config_handlers_s {
  shuso_config_init_fn *initialize;
  shuso_config_set_fn  *set;
}; //shuso_config_handlers_t

struct shuso_module_s {
  const char        *name;
  struct {
    shuso_runtime_handlers_t  runtime;
    shuso_config_handlers_t   config;
  }                  handlers;
  void              *config;
  //who knows what else
};

//the shuso_config struct is designed to be zeroed on initialization
struct shuso_config_s {
  struct {
    const char         *string;
    const char         *filename;
  }                   config;
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

struct shuso_config_file_s {
  const char *path;
  int         fd;
  size_t      sz;
  const char *data;
}; // shuso_config_file_t


struct shuso_common_s {
  shuso_runtime_handlers_t    phase_handlers;
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
  struct {
    lua_State                  *state;
    bool                        external;
    
    lua_reference_t             config_parser;
  }                           lua;
  shuso_stalloc_t             stalloc;
  shuso_shared_slab_t         shm;
  shuso_resolver_t            resolver;
  void                       *data;  //custom data attached to this shuttlesock context
  struct {
    bool                        ready;
    lua_reference_t             index;
  } config;
  struct {
    char                       *msg;
    int                         error_number; //errno
    bool                        allocd;
  }                           error;
  char                        logbuf[1024];
}; //shuso_t;

//shuso_t *shuso_create(unsigned int ev_loop_flags, shuso_runtime_handlers_t *handlers, shuso_config_t *config, const char **err);
shuso_t *shuso_create(const char **err);
shuso_t *shuso_create_with_lua(lua_State *lua, const char **err);

bool shuso_configure_file(shuso_t *S, const char *path);
bool shuso_configure_string(shuso_t *S, const char *str_title, const char *str);
bool shuso_configure_handlers(shuso_t *S, const shuso_runtime_handlers_t *handlers);
bool shuso_configure_finish(shuso_t *S);


bool shuso_destroy(shuso_t *S);
bool shuso_run(shuso_t *S);
bool shuso_stop(shuso_t *S, shuso_stop_t forcefulness);
bool shuso_spawn_manager(shuso_t *S);
bool shuso_stop_manager(shuso_t *S, shuso_stop_t forcefulness);
bool shuso_spawn_worker(shuso_t *S, shuso_process_t *proc);
bool shuso_stop_worker(shuso_t *S, shuso_process_t *proc, shuso_stop_t
 forcefulness);
bool shuso_stop_manager(shuso_t *S, shuso_stop_t forcefulness);

bool shuso_is_master(shuso_t *S);
bool shuso_is_forked_manager(shuso_t *S);

bool shuso_set_log_fd(shuso_t *S, int fd);

bool shuso_set_error(shuso_t *S, const char *fmt, ...);
bool shuso_set_error_errno(shuso_t *S, const char *fmt, ...);
shuso_process_t *shuso_procnum_to_process(shuso_t *S, int procnum);
int shuso_process_to_procnum(shuso_t *S, shuso_process_t *proc);
const char *shuso_process_as_string(shuso_t *S);


#define SHUSO_EACH_WORKER(S, cur) \
  for(shuso_process_t *cur = &S->common->process.worker[S->common->process.workers_start], *___worker_end = &S->common->process.worker[S->common->process.workers_end]; cur < ___worker_end; cur++)
#define shuso_set_nonblocking(fd) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK)

/*
shuso_t *shuso_state(...) -- get shuso state for a variety of inputs, depending on the input types
*/
/* macro wizardry to have type-overloaded variadic super-convenient shuso_state() call */
#define ___SHUSO_STATE_VARARG(_1,_2,NAME,...) NAME
#define shuso_state(...) ___SHUSO_STATE_VARARG(__VA_ARGS__, SHUSO_STATE_2, SHUSO_STATE_1, ___END__VARARG__LIST__)(__VA_ARGS__)

#define ___shuso_state_from_raw_ev_watcher(loop, ev) shuso_state_from_raw_ev_watcher(loop, (ev_watcher *)ev)

#define SHUSO_STATE_1(src) \
  _Generic((src), \
    lua_State *         : shuso_state_from_lua, \
    shuso_ev_io *       : shuso_state_from_ev_io, \
    shuso_ev_timer *    : shuso_state_from_ev_timer, \
    shuso_ev_child *    : shuso_state_from_ev_child, \
    shuso_ev_signal *   : shuso_state_from_ev_signal, \
    ev_watcher *        : shuso_state_from_raw_ev_watcher, \
    ev_timer *          : shuso_state_from_raw_ev_timer, \
    ev_child *          : shuso_state_from_raw_ev_child, \
    ev_signal *         : shuso_state_from_raw_ev_signal \
  )(src)

#define SHUSO_STATE_2(loop, ev) _Generic((ev), \
    shuso_ev_io *       : shuso_state_from_ev_io, \
    shuso_ev_timer *    : shuso_state_from_ev_timer, \
    shuso_ev_child *    : shuso_state_from_ev_child, \
    shuso_ev_signal *   : shuso_state_from_ev_signal, \
    ev_watcher *        : shuso_state_from_raw_ev_watcher, \
    ev_io *             : shuso_state_from_raw_ev_io, \
    ev_timer *          : shuso_state_from_raw_ev_timer, \
    ev_child *          : shuso_state_from_raw_ev_child, \
    ev_signal *         : shuso_state_from_raw_ev_signal, \
    default             : shuso_state_from_raw_ev_dangerously_any \
  )(loop, ev)
/* here endeth the magic. it's not actually wizardry, it's just really ugly */

void shuso_listen(shuso_t *S, shuso_hostinfo_t *bind, shuso_handler_fn handler, shuso_handler_fn cleanup, void *pd);

  
//network utilities
bool shuso_setsockopt(shuso_t *S, int fd, shuso_sockopt_t *opt);
#endif //SHUTTLESOCK_H
