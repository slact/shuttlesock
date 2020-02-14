#ifndef SHUTTLESOCK_IPC_H
#define SHUTTLESOCK_IPC_H
#include <stdatomic.h>
#include <shuttlesock/common.h>
#include <shuttlesock/io.h>

#define SHUTTLESOCK_IPC_CMD_NIL                   0

#define SHUTTLESOCK_IPC_CMD_SIGNAL                1
#define SHUTTLESOCK_IPC_CMD_SHUTDOWN              2
#define SHUTTLESOCK_IPC_CMD_SHUTDOWN_COMPLETE     3
#define SHUTTLESOCK_IPC_CMD_RECONFIGURE           4
#define SHUTTLESOCK_IPC_CMD_RECONFIGURE_RESPONSE  5
#define SHUTTLESOCK_IPC_CMD_SET_LOG_FD            6
#define SHUTTLESOCK_IPC_CMD_OPEN_LISTENER_SOCKETS 7
#define SHUTTLESOCK_IPC_CMD_OPEN_LISTENER_SOCKETS_RESPONSE 8
#define SHUTTLESOCK_IPC_CMD_ALL_WORKERS_STARTED 9
#define SHUTTLESOCK_IPC_CMD_WORKER_STARTED 10
#define SHUTTLESOCK_IPC_CMD_WORKER_STOPPED 11
#define SHUTTLESOCK_IPC_CMD_MANAGER_PROXY_MESSAGE 12
#define SHUTTLESOCK_IPC_CMD_RECEIVE_PROXIED_MESSAGE 13

#define SHUTTLESOCK_IPC_CMD_LUA_MESSAGE 14
#define SHUTTLESOCK_IPC_CMD_LUA_MESSAGE_RESPONSE 15
#define SHUTTLESOCK_IPC_CMD_LUA_MANAGER_PROXY_MESSAGE 16
#define SHUTTLESOCK_IPC_CMD_LUA_MANAGER_RECEIVE_PROXIED_MESSAGE 17

#define SHUTTLESOCK_IPC_CODE_AUTOMATIC UINT16_MAX

#define SHUTTLESOCK_IPC_CODE_AUTOMATIC_MIN 20
#define SHUTTLESOCK_IPC_CODE_AUTOMATIC_MAX UINT8_MAX


// adds a 1-second IPC check for unread left-behind messages.
// only useful for debugging IPC internals
//#define SHUTTLESOCK_DEBUG_IPC_RECEIVE_CHECK_TIMER

typedef struct {
  struct {
    _Atomic int64_t    next_read;
    _Atomic int64_t    next_write_reserve;
    _Atomic int64_t    next_write_release;
  } index;
  atomic_bool        full;
  _Atomic uint8_t    code[256];
  _Atomic(void *)    ptr[256];
} shuso_ipc_ringbuf_t;

typedef struct {
  uint8_t code;
  int     src;
  int     dst;
  void   *pd;
} shuso_ipc_manager_proxy_msg_t;

typedef void shuso_ipc_fn(shuso_t *, const uint8_t code, void *ptr);

typedef struct {
  uint8_t                code;
  shuso_ipc_fn          *receive;
  shuso_ipc_fn          *cancel;
  const char            *name;
} shuso_ipc_handler_t;

typedef struct shuso_ipc_outbuf_s {
  // TODO: pack this struct.
  //it may matter when the IPC is backed up and buffering like crazy
  uint8_t              code;
  void                *ptr;
  shuso_process_t     *dst;
  struct shuso_ipc_outbuf_s *next;
} shuso_ipc_outbuf_t;

typedef void shuso_ipc_receive_fd_fn(shuso_t *S, bool ok, uintptr_t ref, int fd, void *received_pd, void *pd);
typedef struct {
  int      fd;
  void    *pd;
} shuso_ipc_buffered_fd_t;
typedef struct {
  uintptr_t                 ref;
  shuso_ipc_receive_fd_fn  *callback;
  void                     *pd;
  struct {
    shuso_ipc_buffered_fd_t  *array;
    size_t                    count;
  }                         buffered_fds;
  const char               *description;
  shuso_ev_timer            timeout;
  bool                      in_use;
  bool                      finished;
} shuso_ipc_fd_receiver_t;

typedef struct {
  shuso_io_t            notice;
  shuso_io_t            fd;
} shuso_io_send_t;

typedef struct {
  shuso_io_t             notice;
  union {
    uint64_t               eventfd;
    char                   pipe[16];
  }                      buf;
  shuso_io_t             fd;
} shuso_io_receive_t;

typedef struct {
  shuso_ev_timer        send_retry;
#ifdef SHUTTLESOCK_DEBUG_IPC_RECEIVE_CHECK_TIMER
  shuso_ev_timer        receive_check;
#endif
  struct {
    shuso_io_send_t       *send;
    shuso_io_receive_t     receive;
  }                     io;
  
  struct {
    shuso_ipc_outbuf_t   *first;
    shuso_ipc_outbuf_t   *last;
  }                     buf;
  struct {
    shuso_ipc_fd_receiver_t *array;
    size_t                count;
  }                     fd_receiver;
} shuso_ipc_channel_local_t;


typedef struct {
  int                   socket_transfer_fd[2];
  int                   fd[2];
  shuso_ipc_ringbuf_t  *buf;
} shuso_ipc_channel_shared_t;


bool shuso_ipc_commands_init(shuso_t *);
bool shuso_ipc_channel_local_init(shuso_t *);
bool shuso_ipc_channel_local_start(shuso_t *);
bool shuso_ipc_channel_local_stop(shuso_t *);
bool shuso_ipc_channel_shared_create(shuso_t *, shuso_process_t *);
bool shuso_ipc_channel_shared_destroy(shuso_t *, shuso_process_t *);
bool shuso_ipc_channel_shared_start(shuso_t *, shuso_process_t *);
bool shuso_ipc_channel_shared_stop(shuso_t *, shuso_process_t *);


bool shuso_ipc_send(shuso_t *, shuso_process_t *dst, const uint8_t code, void *ptr);
bool shuso_ipc_send_workers(shuso_t *, const uint8_t code, void *ptr);
const shuso_ipc_handler_t *shuso_ipc_add_handler(shuso_t *S,  const char *name, uint32_t code, shuso_ipc_fn *receive, shuso_ipc_fn *cancel);


bool shuso_ipc_send_fd(shuso_t *, shuso_process_t *, int fd, uintptr_t ref, void *pd);

//TODO: change to shuso_ipc_receive_fd_start with cleanup callback
bool shuso_ipc_receive_fd_start(shuso_t *S, const char *description,  float timeout_sec, shuso_ipc_receive_fd_fn *callback, uintptr_t ref, void *pd);
bool shuso_ipc_receive_fd_finish(shuso_t *S, uintptr_t ref);

//some built-in IPC commands

typedef void (shuso_ipc_open_sockets_fn)(shuso_t *, shuso_status_t status, shuso_hostinfo_t *, int *sockets, int socket_count, void *pd);

bool shuso_ipc_command_open_listener_sockets(shuso_t *, shuso_hostinfo_t *, int count, shuso_sockopts_t *, shuso_ipc_open_sockets_fn *callback, void *pd);


#endif //SHUTTLESOCK_IPC_H
