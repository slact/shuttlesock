#ifndef SHUTTLESOCK_IO_H
#define SHUTTLESOCK_IO_H

#include <shuttlesock/common.h>
#include <sys/uio.h>
#include <sys/socket.h>

typedef struct shuso_io_s shuso_io_t;

typedef void shuso_io_fn(shuso_t *S, shuso_io_t *io, int error);

typedef enum {
  SHUSO_IO_OP_READV,
  SHUSO_IO_OP_WRITEV,
  SHUSO_IO_OP_READ,
  SHUSO_IO_OP_WRITE,
  SHUSO_IO_OP_SENDMSG,
  SHUSO_IO_OP_RECVMSG
} shuso_io_opcode_t;

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
  void             *privdata;
  
  bool              busy;
  
  //everything else is private, more or less
  uint8_t           op_code;
  unsigned          op_io_vector:1;
  unsigned          op_repeat_to_completion:1;
  unsigned          op_registered_memory_buffer:1;
  uint16_t          coroutine_stage;
#ifdef SHUTTLESOCK_DEBUG_IO
  shuso_fn_debug_info_t creator;
  shuso_fn_debug_info_t runner;
  shuso_fn_debug_info_t op_caller;
#endif
  
  shuso_t          *S;
  shuso_io_fn      *callback;
  shuso_ev_io       watcher;
};

#define SHUSO_IO_UNCHANGED_CALLBACK ((shuso_io_fn *)-1)
#define SHUSO_IO_UNCHANGED_PRIVDATA ((void *)-1)

void shuso_io_init_fd(shuso_t *S, shuso_io_t *io, int fd);
void shuso_io_init_socket(shuso_t *S, shuso_io_t *io, shuso_socket_t *socket);
void shuso_io_set_callback(shuso_io_t *io, shuso_io_fn *cb);
void shuso_io_set_privdata(shuso_io_t *io, void *pd);


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

#ifdef SHUTTLESOCK_DEBUG_IO
#define shuso_io_coro_init(shuso_state, io, filedes, coroutine_fn) do { \
  *(io) = (shuso_io_t *) { \
    creator.name = __FUNCTION__, \
    creator.file = __FILE__, \
    creator.line = __LINE__, \
    S = shuso_state, \
    callback = coroutine_fn, \
    fd = filedes
  } \
} while(0)  
#else
#define shuso_io_coro_init(shuso_state, io, filedes, coroutine_fn) do { \
  *(io) = (shuso_io_t *) { \
    S = shuso_state, \
    callback = coroutine_fn, \
    fd = filedes \
  } \
} while(0)
#endif


#define ___SHUSO_IO_CORO_BEGIN(io) \
shuso_io_t *___coroutine_io_struct = io; \
switch(io->coroutine_stage) { \
  case 0: {

#define ___SHUSO_IO_CORO_YIELD(io_operation, arg1, arg2) \
    ___coroutine_io_struct->coroutine_stage = __LINE__; \
    shuso_io_ ## io_operation (___coroutine_io_struct, arg1, arg2, SHUSO_IO_UNCHANGED_CALLBACK, SHUSO_IO_UNCHANGED_PRIVDATA); \
    return; \
  } \
  case __LINE__: {

#ifdef SHUTTLESOCK_DEBUG_IO

#define SHUSO_IO_CORO_BEGIN(io) \
  io->runner.name = __FUNCTION__; \
  io->runner.file = __FILE__; \
  io->runner.line = __LINE__; \
  ___SHUSO_IO_CORO_BEGIN(io)
  
#define SHUSO_IO_CORO_YIELD(io_operation, arg1, arg2) \
  io->op_caller.name = __FUNCTION__; \
  io->op_caller.file = __FILE__; \
  io->op_caller.line = __LINE__;
  ___SHUSO_IO_CORO_YIELD(io_operation, arg1, arg2)
#else
  
#define SHUSO_IO_CORO_BEGIN(io) \
  ___SHUSO_IO_CORO_BEGIN(io)
  
#define SHUSO_IO_CORO_YIELD(io_operation, arg1, arg2) \
  ___SHUSO_IO_CORO_YIELD(io_operation, arg1, arg2)
#endif

#define SHUSO_IO_CORO_END \
  } \
}
  




#endif // SHUTTLESOCK_IO_H
