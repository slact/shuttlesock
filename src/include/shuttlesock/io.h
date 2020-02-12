#ifndef SHUTTLESOCK_IO_H
#define SHUTTLESOCK_IO_H

#include <shuttlesock/common.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct shuso_io_s shuso_io_t;

typedef void shuso_io_fn(shuso_t *S, shuso_io_t *io);

#define SHUSO_IO_READ 1
#define SHUSO_IO_WRITE 2

typedef enum {
  SHUSO_IO_OP_NONE,
  SHUSO_IO_OP_READV,
  SHUSO_IO_OP_WRITEV,
  SHUSO_IO_OP_READ,
  SHUSO_IO_OP_WRITE,
  SHUSO_IO_OP_SENDMSG,
  SHUSO_IO_OP_RECVMSG
} shuso_io_opcode_t;

typedef enum {
  SHUSO_IO_WATCH_NONE = 0,
  SHUSO_IO_WATCH_OP_FINISH,
  SHUSO_IO_WATCH_OP_RETRY,
  SHUSO_IO_WATCH_POLL_READ,
  SHUSO_IO_WATCH_POLL_WRITE,
  SHUSO_IO_WATCH_POLL_READWRITE,
} shuso_io_watch_type_t;

struct shuso_io_s {
  int               fd;
  union {
    struct msghdr  *msg;
    struct iovec   *iov;
    char           *buf;
  };
  union {
    int     iovcnt;
    size_t  len;
    int     flags;
  };
  
  ssize_t           result;
  int               error;
  void             *privdata;
  
  bool              busy;
  
  //everything else is private, more or less
  shuso_io_watch_type_t watch_type;
  unsigned          readwrite:2;
  unsigned          use_io_uring:1;
  
  unsigned          op_io_vector:1;
  unsigned          op_repeat_to_completion:1;
  unsigned          op_registered_memory_buffer:1;
  
  uint8_t           op_code;
  uint16_t          coroutine_stage;
#ifdef SHUTTLESOCK_DEBUG_IO
  shuso_fn_debug_info_t runner;
  shuso_fn_debug_info_t op_caller;
#endif
  
  shuso_t          *S;
  shuso_io_fn      *callback;
  
  shuso_ev_io       watcher;
};

#define SHUSO_IO_UNCHANGED_CALLBACK ((shuso_io_fn *)-1)
#define SHUSO_IO_UNCHANGED_PRIVDATA ((void *)-1)

#ifdef SHUTTLESOCK_DEBUG_IO
#define shuso_io_coro_init(...) do { \
  __shuso_io_coro_init(__VA_ARGS__); \
  io->creator.name = __FUNCTION__; \
  io->creator.file = __FILE__; \
  io->creator.line = __LINE__; \
} while(0)  
#else
#define shuso_io_coro_init(...) \
  __shuso_io_coro_init(__VA_ARGS__)
#endif
  
void __shuso_io_coro_init(shuso_t *S, shuso_io_t *io, int fd, int readwrite, shuso_io_fn *coro, void *privdata);

#define shuso_io_coro_start(io, data, int_data) \
  assert(io->coroutine_stage == 0); \
  shuso_io_coro_resume(io, data, int_data)

#define shuso_io_coro_resume(io, data, int_data) \
_Generic((data), \
  char *         : shuso_io_coro_resume_buf, \
  void *         : shuso_io_coro_resume_buf, \
  struct iovec * : shuso_io_coro_resume_iovec, \
  struct msghdr *: shuso_io_coro_resume_msg \
)(io, data, int_data)

bool shuso_io_coro_fresh(shuso_io_t *io);

void shuso_io_coro_resume_buf(shuso_io_t *io, char *buf, size_t len);
void shuso_io_coro_resume_iovec(shuso_io_t *io, struct iovec *iov, int iovcnt);
void shuso_io_coro_resume_msg(shuso_io_t *io, struct msghdr *msg, int iovcnt);

void shuso_io_buffered_coro_resume(shuso_io_t *io);

void shuso_io_writev(shuso_io_t *io, struct iovec *iov, int iovcnt, shuso_io_fn *cb, void *pd);
void shuso_io_readv(shuso_io_t *io, struct iovec *iov, int iovcnt, shuso_io_fn *cb, void *pd);

void shuso_io_writev_partial(shuso_io_t *io, struct iovec *iov, int iovcnt, shuso_io_fn *cb, void *pd);
void shuso_io_readv_partial(shuso_io_t *io, struct iovec *iov, int iovcnt, shuso_io_fn *cb, void *pd);

void shuso_io_write(shuso_io_t *io, const void *buf, size_t len, shuso_io_fn *cb, void *pd);
void shuso_io_read(shuso_io_t *io, void *buf, size_t len, shuso_io_fn *cb, void *pd);

void shuso_io_write_partial(shuso_io_t *io, const void *buf, size_t len, shuso_io_fn *cb, void *pd);
void shuso_io_read_partial(shuso_io_t *io, void *buf, size_t len, shuso_io_fn *cb, void *pd);

void shuso_io_connect(shuso_io_t *io, const shuso_socket_t *socket, shuso_io_fn *cb, void *pd);
void shuso_io_accept(shuso_io_t *io, shuso_socket_t *socket, shuso_io_fn *cb, void *pd);

void shuso_io_sendmsg(shuso_io_t *io, struct msghdr *msg, int flags, shuso_io_fn *cb, void *pd);
void shuso_io_recvmsg(shuso_io_t *io, struct msghdr *msg, int flags, shuso_io_fn *cb, void *pd);

void shuso_io_close(shuso_io_t *io);


#define ___SHUSO_IO_CORO_BEGIN(io) \
shuso_io_t *___coroutine_io_struct = io; \
switch(io->coroutine_stage) { \
  case 0:

#define ___SHUSO_IO_CORO_YIELD(io_operation, arg1, arg2) \
    ___coroutine_io_struct->coroutine_stage = __LINE__; \
    shuso_io_ ## io_operation (___coroutine_io_struct, arg1, arg2, SHUSO_IO_UNCHANGED_CALLBACK, SHUSO_IO_UNCHANGED_PRIVDATA); \
    return; \
  case __LINE__:

#ifdef SHUTTLESOCK_DEBUG_IO

#define SHUSO_IO_CORO_BEGIN(io) \
  io->runner.name = __FUNCTION__; \
  io->runner.file = __FILE__; \
  io->runner.line = __LINE__; \
  ___SHUSO_IO_CORO_BEGIN(io)
  
#define SHUSO_IO_CORO_YIELD(io_operation, arg1, arg2) \
  ___coroutine_io_struct->op_caller.name = __FUNCTION__; \
  ___coroutine_io_struct->op_caller.file = __FILE__; \
  ___coroutine_io_struct->op_caller.line = __LINE__;
  ___SHUSO_IO_CORO_YIELD(io_operation, arg1, arg2)
#else
  
#define SHUSO_IO_CORO_BEGIN(io) \
  ___SHUSO_IO_CORO_BEGIN(io)
  
#define SHUSO_IO_CORO_YIELD(io_operation, arg1, arg2) \
  ___SHUSO_IO_CORO_YIELD(io_operation, arg1, arg2)

#endif

#define SHUSO_IO_CORO_END \
} \
___coroutine_io_struct->coroutine_stage = 0
  




#endif // SHUTTLESOCK_IO_H
