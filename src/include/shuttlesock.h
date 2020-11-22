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
#include <shuttlesock/common.h>
#include <shuttlesock/watchers.h>
#include <shuttlesock/io.h>
#include <shuttlesock/sbuf.h>
#include <shuttlesock/llist.h>
#include <shuttlesock/ipc.h>
#include <shuttlesock/pool.h>
#include <shuttlesock/resolver.h>
#include <shuttlesock/shared_slab.h>
#include <shuttlesock/log.h>
#include <shuttlesock/buffer.h>
#include <shuttlesock/module.h>
#include <shuttlesock/lua_utils.h>
#include <shuttlesock/sysutil.h>
#include <shuttlesock/event.h>
#include <shuttlesock/core_modules.h>
#include <shuttlesock/buffer.h>
#include <shuttlesock/instring.h>
#ifdef SHUTTLESOCK_HAVE_IO_URING
#include <liburing.h>
#endif

typedef struct shuso_process_s {
  shuso_t                          *S;
  pid_t                             pid;
  pthread_t                         tid;
  int                               procnum;
  _Atomic(shuso_runstate_t)        *state;
  uint16_t                          generation;
  shuso_ipc_channel_shared_t        ipc;
} shuso_process_t;

//params for setsockopt()
typedef struct shuso_sockopt_s {
  int           level;
  int           name;
  union {
    int           integer;
    int           flag;
    struct timeval timeval;
    struct linger linger;
  }             value;
} shuso_sockopt_t;

typedef struct shuso_sockopts_s {
  size_t           count;
  shuso_sockopt_t *array;
} shuso_sockopts_t;

//the shuso_config struct is designed to be zeroed on initialization
typedef struct shuso_config_s {
  struct {
    const char         *string;
    const char         *filename;
  }                   config;
  struct {          //ipc
    float               send_retry_delay;
    float               send_timeout;
  }                   ipc;
  struct {
    int                 enabled;
    int                 worker_entries;
    int                 sqpoll_thread;          
    int                 sqpoll_thread_idle;
  }                   io_uring;
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
} shuso_config_t;

typedef struct {
  pid_t           pid;
  enum {
                    SHUSO_CHILD_RUNNING = 0,
                    SHUSO_CHILD_EXITED, //WIFEXITED(status)
                    SHUSO_CHILD_KILLED, //WIFSIGNALED(status)
                    SHUSO_CHILD_STOPPED, //WIFSTOPPED(status)
  }               state;
  union {
    int           code; //WEXITSTATUS(status)
    int           signal; //WTERMSIG(status), WSTOPSIG(status)
  };
  int             waitpid_status;
} shuso_sigchild_info_t;

typedef struct shuso_common_s {
  shuso_runstate_t    state;
  shuso_ipc_handler_t ipc_handlers[256];
  struct {
    shuso_config_module_common_ctx_t  *config;
    shuso_core_module_common_ctx_t    *core;
  }                   ctx;
  
  shuso_config_t      config;
  struct {
    size_t              count;
    shuso_module_t    **array;
    void              **events;
  }                   modules;
  struct {          //process
    shuso_process_t     master;
    shuso_process_t     manager;
    shuso_process_t     worker[SHUTTLESOCK_MAX_WORKERS];
    _Atomic(uint16_t)  *workers_start;
    _Atomic(uint16_t)  *workers_end;
    bool                all_workers_running; //only relevant on manager
    struct {
      shuso_sigchild_info_t manager;
      shuso_sigchild_info_t last;
    }                 sigchild;
  }                   process;
  struct {          //log
    int                 fd;
  }                   log;
  shuso_shared_slab_t shm;
  bool                master_has_root;
} shuso_common_t;

_Static_assert(offsetof(shuso_common_t, process.worker)+sizeof(shuso_process_t)*SHUTTLESOCK_MASTER == offsetof(shuso_common_t, process.master), "master process offset does not match value of SHUTTLESOCK_MASTER");
_Static_assert(offsetof(shuso_common_t, process.worker)+sizeof(shuso_process_t)*SHUTTLESOCK_MANAGER == offsetof(shuso_common_t, process.manager), "manager process offset does not match value of SHUTTLESOCK_MANAGER");

LLIST_TYPEDEF_LINK_STRUCT(shuso_ev_timer);

typedef struct shuso_s {
  int                         procnum;
  shuso_process_t            *process;
  shuso_ipc_channel_local_t   ipc;
  struct {                  //ev
    shuso_loop                 *loop;
    unsigned int                flags;
  }                           ev;
  struct {
    bool                        on;
#ifdef SHUTTLESOCK_HAVE_IO_URING
    int                         eventfd;
    struct io_uring             ring;
    struct {
      shuso_io_uring_handle_t    *head;
      shuso_io_uring_handle_t    *tail;
    }                           sqe_request_queue;
    shuso_ev_io                 watcher;
#endif
  }                           io_uring;
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
  shuso_pool_t                pool;
  shuso_shared_slab_t         shm;
  shuso_resolver_t            resolver;
  const shuso_module_t       *active_module;
  struct {
    shuso_core_module_ctx_t     core;
    shuso_config_module_ctx_t   config;
  }                           ctx;
  void                       *data;  //custom data attached to this shuttlesock context
  struct {
    bool                        ready;
    lua_reference_t             index;
  } config;
  struct {
    char                       *msg;
    int                         error_count;
    int                         error_number; //errno
    bool                        static_memory;
    bool                        do_not_log;
    bool                        do_not_publish_event;
  }                           error;
  char                        logbuf[1024];
} shuso_t;

//shuso_t *shuso_create(unsigned int ev_loop_flags, shuso_runtime_handlers_t *handlers, shuso_config_t *config, const char **err);
shuso_t *shuso_create(const char **err);
shuso_t *shuso_create_with_lua(lua_State *lua, const char **err);

bool shuso_runstate_check(shuso_t *S, shuso_runstate_t allowed_state, const char *whatcha_doing);
bool shuso_configure_finish(shuso_t *S);

bool shuso_master_has_root(shuso_t *S);

bool shuso_destroy(shuso_t *S);
bool shuso_run(shuso_t *S);
bool shuso_stop(shuso_t *S, shuso_stop_t forcefulness);
bool shuso_stop_manager(shuso_t *S, shuso_stop_t forcefulness);
bool shuso_stop_worker(shuso_t *S, shuso_process_t *proc, shuso_stop_t
 forcefulness);
bool shuso_stop_manager(shuso_t *S, shuso_stop_t forcefulness);

bool shuso_is_master(shuso_t *S);
bool shuso_is_manager(shuso_t *S);
bool shuso_is_worker(shuso_t *S);

bool shuso_set_log_fd(shuso_t *S, int fd);

bool shuso_set_error(shuso_t *S, const char *fmt, ...);
bool shuso_set_error_errno(shuso_t *S, const char *fmt, ...);

void shuso_set_error_vararg(shuso_t *S, const char *fmt, va_list args);
void shuso_set_error_errno_vararg(shuso_t *S, const char *fmt, va_list args);

int shuso_error_count(shuso_t *S);

int shuso_error_capture_start(shuso_t *S);
const char *shuso_error_capture_finish(shuso_t *S, int prev_errcount);

const char *shuso_last_error(shuso_t *S);

int shuso_last_errno(shuso_t *S);
shuso_process_t *shuso_process(shuso_t *S, int procnum);
int shuso_process_to_procnum(shuso_t *S, shuso_process_t *proc);
bool shuso_procnum_valid(shuso_t *S, int procnum, const char **err);
bool shuso_processes_share_heap(shuso_t *S, int procnum1, int procnum2);

const char *shuso_process_as_string(int procnum);
const char *shuso_runstate_as_string(shuso_runstate_t state);

int shuso_workers_count(shuso_t *S);

typedef struct shuso_fn_debug_info_s {
  const char *name;
  const char *file;
  int         line;
} shuso_fn_debug_info_t;

#define SHUSO_EACH_WORKER(S, cur) \
  for(shuso_process_t *cur = &S->common->process.worker[*S->common->process.workers_start], *___worker_end = &S->common->process.worker[*S->common->process.workers_end]; cur < ___worker_end; cur++)
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
socklen_t shuso_sockaddr_len(shuso_sockaddr_t *);

#endif //SHUTTLESOCK_H
