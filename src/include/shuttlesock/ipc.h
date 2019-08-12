#ifndef SHUTTLESOCK_IPC_H
#define SHUTTLESOCK_IPC_H
#include <stdatomic.h>
#include <shuttlesock/common.h>

#define SHUTTLESOCK_IPC_CMD_NIL                   0
#define SHUTTLESOCK_IPC_CMD_SIGNAL                1
#define SHUTTLESOCK_IPC_CMD_SHUTDOWN              2
#define SHUTTLESOCK_IPC_CMD_SHUTDOWN_COMPLETE     3
#define SHUTTLESOCK_IPC_CMD_RECONFIGURE           4
#define SHUTTLESOCK_IPC_CMD_RECONFIGURE_RESPONSE  5
#define SHUTTLESOCK_IPC_CMD_SET_LOG_FD            6
#define SHUTTLESOCK_IPC_CMD_OPEN_LISTENER_SOCKETS 7
#define SHUTTLESOCK_IPC_CMD_OPEN_LISTENER_SOCKETS_RESPONSE 8


typedef struct {
  _Atomic uint8_t    next_read;
  _Atomic uint8_t    next_reserve;
  _Atomic uint8_t    next_release;
  _Atomic uint8_t    code[256];
  _Atomic(void *)    ptr[256];
} shuso_ipc_ringbuf_t;

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

typedef void shuso_ipc_receive_fd_fn(shuso_t *ctx, bool ok, uintptr_t ref, int fd, void *received_pd, void *pd);
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
  ev_timer                  timeout;
} shuso_ipc_fd_receiver_t;

typedef struct {
  ev_timer              send_retry;
  struct {
    shuso_ipc_outbuf_t   *first;
    shuso_ipc_outbuf_t   *last;
  }                     buf;
  struct {
    shuso_ipc_fd_receiver_t *array;
    size_t                count;
  }                     fd_receiver;
  ev_io                 receive;
  ev_io                 socket_transfer_receive;
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


bool shuso_ipc_send(shuso_t *, shuso_process_t *, const uint8_t code, void *ptr);
bool shuso_ipc_send_workers(shuso_t *, const uint8_t code, void *ptr);
bool shuso_ipc_add_handler(shuso_t *,  const char *name, const uint8_t code, shuso_ipc_fn *, shuso_ipc_fn *);


bool shuso_ipc_send_fd(shuso_t *, shuso_process_t *, int fd, uintptr_t ref, void *pd);

bool shuso_ipc_receive_fd_start(shuso_t *ctx, const char *description,  float timeout_sec, shuso_ipc_receive_fd_fn *callback, uintptr_t ref, void *pd);
bool shuso_ipc_receive_fd_finish(shuso_t *ctx, uintptr_t ref);

//some built-in IPC commands

typedef void (shuso_ipc_open_sockets_fn)(shuso_t *, shuso_status_t status, shuso_hostinfo_t *, int *sockets, int socket_count, void *pd);

bool shuso_ipc_command_open_listener_sockets(shuso_t *, shuso_hostinfo_t *, int count, shuso_sockopts_t *, shuso_ipc_open_sockets_fn *callback, void *pd);


#endif //SHUTTLESOCK_IPC_H
