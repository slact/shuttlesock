#ifndef SHUTTLESOCK_IPC_H
#define SHUTTLESOCK_IPC_H
#include <stdatomic.h>

#define SHUTTLESOCK_IPC_CMD_NIL                   0
#define SHUTTLESOCK_IPC_CMD_SIGNAL                1
#define SHUTTLESOCK_IPC_CMD_SHUTDOWN              2
#define SHUTTLESOCK_IPC_CMD_SHUTDOWN_COMPLETE     3
#define SHUTTLESOCK_IPC_CMD_RECONFIGURE           4
#define SHUTTLESOCK_IPC_CMD_RECONFIGURE_RESPONSE  5
#define SHUTTLESOCK_IPC_CMD_SET_LOG_FD            6
#define SHUTTLESOCK_IPC_CMD_OPEN_LISTENER_SOCKETS 7
#define SHUTTLESOCK_IPC_CMD_OPEN_LISTENER_SOCKETS_RESPONSE 8

struct shuso_s;
struct shuso_process_s;
struct shuso_hostinfo_s;

typedef struct {
  _Atomic uint8_t    next_read;
  _Atomic uint8_t    next_reserve;
  _Atomic uint8_t    next_release;
  _Atomic uint8_t    code[256];
  _Atomic(void *)    ptr[256];
} shuso_ipc_ringbuf_t;

typedef void shuso_ipc_fn(struct shuso_s *, const uint8_t code, void *ptr);

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
  struct shuso_process_s *dst;
  struct shuso_ipc_outbuf_s *next;
} shuso_ipc_outbuf_t;

typedef void shuso_ipc_receive_fd_fn(struct shuso_s *ctx, bool ok, uintptr_t ref, int fd, void *received_pd, void *pd);
typedef struct {
  uintptr_t                 ref;
  shuso_ipc_receive_fd_fn  *callback;
  void                     *pd;
  struct {
    int                      *array;
    size_t                    count;
  }                         buffered_fds;
  const char               *description;
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
  ev_io                 socketpipe_receive;
} shuso_ipc_channel_local_t;


typedef struct {
  int                   fd_socketpipe[2];
  int                   fd[2];
  shuso_ipc_ringbuf_t  *buf;
} shuso_ipc_channel_shared_t;


bool shuso_ipc_commands_init(struct shuso_s *);
bool shuso_ipc_channel_local_init(struct shuso_s *);
bool shuso_ipc_channel_local_start(struct shuso_s *);
bool shuso_ipc_channel_local_stop(struct shuso_s *);
bool shuso_ipc_channel_shared_create(struct shuso_s *, struct shuso_process_s *);
bool shuso_ipc_channel_shared_destroy(struct shuso_s *, struct shuso_process_s *);
bool shuso_ipc_channel_shared_start(struct shuso_s *, struct shuso_process_s *);
bool shuso_ipc_channel_shared_stop(struct shuso_s *, struct shuso_process_s *);


bool shuso_ipc_send(struct shuso_s *, struct shuso_process_s *, const uint8_t code, void *ptr);
bool shuso_ipc_send_workers(struct shuso_s *, const uint8_t code, void *ptr);
bool shuso_ipc_add_handler(struct shuso_s *,  const char *name, const uint8_t code, shuso_ipc_fn *, shuso_ipc_fn *);


bool shuso_ipc_send_fd(struct shuso_s *, struct shuso_process_s *, int fd, uintptr_t ref, void *pd);

bool shuso_ipc_receive_fd_start(struct shuso_s *ctx, const char *description, shuso_ipc_receive_fd_fn *callback, uintptr_t ref, void *pd);
bool shuso_ipc_receive_fd_finish(struct shuso_s *ctx, uintptr_t ref);

//some built-in IPC commands

bool shuso_ipc_command_open_listener_sockets(struct shuso_s *, struct shuso_hostinfo_s *, int count, void (*callback)(bool ok, struct shuso_s *, struct shuso_hostinfo_s *, int *sockets, int socket_count, void *pd), void *pd);


#endif //SHUTTLESOCK_IPC_H
