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
#define SHUTTLESOCK_IPC_CMD_LISTEN_PORT           7
#define SHUTTLESOCK_IPC_CMD_LISTEN_PORT_RESPONSE  8

struct shuso_s;
struct shuso_process_s;

typedef struct {
  _Atomic uint8_t    next_read;
  _Atomic uint8_t    next_reserve;
  _Atomic uint8_t    next_release;
  _Atomic uint8_t    code[256];
  _Atomic(void *)    ptr[256];
} shuso_ipc_ringbuf_t;

typedef void shuso_ipc_receive_fn(struct shuso_s *, const uint8_t code, void *ptr);
typedef void shuso_ipc_cancel_fn(struct shuso_s *, const uint8_t code, void *ptr);

typedef struct {
  uint8_t                code;
  shuso_ipc_receive_fn  *receive;
  shuso_ipc_cancel_fn   *cancel;
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

typedef struct {
  ev_timer              send_retry;
  struct {
    shuso_ipc_outbuf_t   *first;
    shuso_ipc_outbuf_t   *last;
  }                     buf;
} shuso_ipc_channel_local_t;

typedef struct {
  int                   fd_socketpipe[2];
  int                   fd[2];
  ev_io                 receive;
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
bool shuso_ipc_add_handler(struct shuso_s *,  const char *name, const uint8_t code, shuso_ipc_receive_fn *, shuso_ipc_cancel_fn *);

#endif //SHUTTLESOCK_IPC_H
