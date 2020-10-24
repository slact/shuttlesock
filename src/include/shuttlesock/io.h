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
  SHUSO_IO_OP_NONE = 0,
  SHUSO_IO_OP_READV,
  SHUSO_IO_OP_WRITEV,
  SHUSO_IO_OP_READ,
  SHUSO_IO_OP_WRITE,
  SHUSO_IO_OP_SENDMSG,
  SHUSO_IO_OP_SENDTO,
  SHUSO_IO_OP_SEND,
  SHUSO_IO_OP_RECVMSG,
  SHUSO_IO_OP_RECVFROM,
  SHUSO_IO_OP_RECV,
  SHUSO_IO_OP_ACCEPT,
  SHUSO_IO_OP_CONNECT,
  SHUSO_IO_OP_CLOSE,
  SHUSO_IO_OP_SHUTDOWN
} shuso_io_opcode_t;

typedef enum {
  SHUSO_IO_WATCH_NONE = 0,
  SHUSO_IO_WATCH_OP_FINISH,
  SHUSO_IO_WATCH_OP_RETRY,
  SHUSO_IO_WATCH_POLL_READ,
  SHUSO_IO_WATCH_POLL_WRITE,
  SHUSO_IO_WATCH_POLL_READWRITE,
} shuso_io_watch_type_t;

typedef struct shuso_io_s {
  shuso_t          *S;
  shuso_socket_t    io_socket;
  
  union {
    struct msghdr    *msg;
    struct iovec     *iov;
    char             *buf;
    shuso_socket_t   *socket;
    shuso_hostinfo_t *hostinfo; 
    void             *result_data;
  };
  union {
    shuso_sockaddr_t *sockaddr; //needs to be separate because sendto/recvfrom use a buffer _and_ a sockaddr
    struct iovec     *incomplete_original_iovec; //used for writev/readv/sendmsg/recvmsg
  };
  union {
    size_t            iovcnt;
    ssize_t           len;
    int               intdata;
  };
  union {
    int               flags; //needs to be separate because sendto/recvfrom takes flags _and_ a length
    struct iovec     *incomplete_temporary_iovec; //used for writev/readv/sendmsg/recvmsg
  };
  union {
    ssize_t           result;
    int               result_fd;
  };
  int               error;
  shuso_io_fn      *handler;
  shuso_io_fn      *error_handler;
  void             *privdata;
  uint16_t          handler_stage;
  uint8_t           closed;
  
  //everything else is private, more or less
  union {
    shuso_ev_io       watcher;
    shuso_io_uring_handle_t io_uring_handle;
  };
  
  uint8_t           opcode;
  uint8_t           watch_type;
  unsigned          readwrite:2;
  unsigned          use_io_uring:1;
  
  unsigned          op_again:1;
  unsigned          op_repeat_to_completion:1;
  unsigned          op_registered_memory_buffer:1;
  
#ifdef SHUTTLESOCK_DEBUG_IO
  shuso_fn_debug_info_t runner;
  shuso_fn_debug_info_t op_caller;
#endif
} shuso_io_t;

#ifdef SHUTTLESOCK_DEBUG_IO
#define shuso_io_init(...) do { \
  __shuso_io_init(__VA_ARGS__); \
  io->creator.name = __FUNCTION__; \
  io->creator.file = __FILE__; \
  io->creator.line = __LINE__; \
} while(0)  
#else
#define shuso_io_init(S, io, sockdata, ...) \
_Generic((sockdata), \
  shuso_hostinfo_t *      : __shuso_io_init_hostinfo, \
  shuso_socket_t *        : __shuso_io_init_socket, \
  default                 : __shuso_io_init_fd \
)(S, io, sockdata, __VA_ARGS__)
#endif

void __shuso_io_init_hostinfo(shuso_t *S, shuso_io_t *io, shuso_hostinfo_t *, int readwrite, shuso_io_fn *coro, void *privdata);
void __shuso_io_init_socket(shuso_t *S, shuso_io_t *io, shuso_socket_t *, int readwrite, shuso_io_fn *coro, void *privdata);
void __shuso_io_init_fd(shuso_t *S, shuso_io_t *io, int fd, int readwrite, shuso_io_fn *coro, void *privdata);


#define ___SHUSO_IO_CORO_START_VARARG(_1,_2,_3,NAME,...) NAME
#define shuso_io_start(...) ___SHUSO_IO_CORO_START_VARARG(__VA_ARGS__, SHUSO_IO_CORO_START_3, SHUSO_IO_CORO_START_2, SHUSO_IO_CORO_START_1)(__VA_ARGS__)

#define SHUSO_IO_CORO_START_3(io, data, int_data) \
  assert((io)->handler_stage == 0); \
  shuso_io_resume(io, data, int_data)

#define SHUSO_IO_CORO_START_1(io) \
  assert((io)->handler_stage == 0); \
  shuso_io_resume_buf(io, NULL, 0)

  
#define ___SHUSO_IO_CORO_RESUME_VARARG(_1,_2,_3,NAME,...) NAME
#define shuso_io_resume(...) ___SHUSO_IO_CORO_RESUME_VARARG(__VA_ARGS__, SHUSO_IO_CORO_RESUME_3, SHUSO_IO_CORO_RESUME_2, SHUSO_IO_CORO_RESUME_1)(__VA_ARGS__)
  
#define SHUSO_IO_CORO_RESUME_3(io, data, int_data) \
_Generic((data), \
  char *         : shuso_io_resume_buf, \
  void *         : shuso_io_resume_buf, \
  struct iovec * : shuso_io_resume_iovec, \
  struct msghdr *: shuso_io_resume_msg \
)(io, data, int_data)

#define SHUSO_IO_CORO_RESUME_1(io) shuso_io_resume_buf(io, NULL, 0)

void shuso_io_stop(shuso_io_t *io);
void shuso_io_abort(shuso_io_t *io);

void shuso_io_resume_buf(shuso_io_t *io, char *buf, size_t len);
void shuso_io_resume_iovec(shuso_io_t *io, struct iovec *iov, int iovcnt);
void shuso_io_resume_msg(shuso_io_t *io, struct msghdr *msg, int iovcnt);
void shuso_io_resume_data(shuso_io_t *io, void *data, int int_data);

void shuso_io_writev(shuso_io_t *io, struct iovec *iov, int iovcnt);
void shuso_io_readv(shuso_io_t *io, struct iovec *iov, int iovcnt);

void shuso_io_writev_partial(shuso_io_t *io, struct iovec *iov, int iovcnt);
void shuso_io_readv_partial(shuso_io_t *io, struct iovec *iov, int iovcnt);

void shuso_io_write(shuso_io_t *io, const void *buf, size_t len);
void shuso_io_read(shuso_io_t *io, void *buf, size_t len);

void shuso_io_write_partial(shuso_io_t *io, const void *buf, size_t len);
void shuso_io_read_partial(shuso_io_t *io, void *buf, size_t len);

void shuso_io_send(shuso_io_t *io, const void *buf, size_t len, int flags);
void shuso_io_recv(shuso_io_t *io, void *buf, size_t len, int flags);

void shuso_io_sendto(shuso_io_t *io, const void *buf, size_t len, shuso_sockaddr_t *to, int flags);
void shuso_io_sendto_partial(shuso_io_t *io, const void *buf, size_t len, shuso_sockaddr_t *to, int flags);

void shuso_io_recvfrom_partial(shuso_io_t *io, void *buf, size_t len, shuso_sockaddr_t *from, int flags);
void shuso_io_recvfrom(shuso_io_t *io, void *buf, size_t len, shuso_sockaddr_t *from, int flags);

void shuso_io_connect(shuso_io_t *io);
void shuso_io_accept(shuso_io_t *io, shuso_sockaddr_t *sockaddr_buffer, socklen_t socklen);
void shuso_io_close(shuso_io_t *io);
void shuso_io_shutdown(shuso_io_t *io, int rw);

void shuso_io_sendmsg(shuso_io_t *io, struct msghdr *msg, int flags);
void shuso_io_recvmsg(shuso_io_t *io, struct msghdr *msg, int flags);

void shuso_io_suspend(shuso_io_t *io, void *);

void shuso_io_wait(shuso_io_t *io, int evflags);

void shuso_io_close(shuso_io_t *io);


#define SHUSO_IO_ERROR_HANDLER (___handler_io_struct->error_handler)

#define ___SHUSO_IO_CORO_BEGIN(io) \
shuso_io_t *___handler_io_struct = io; \
switch(io->handler_stage) { \
  case 0:

#define SHUSO_IO_CORO_YIELD(...) ___SHUSO_IO_CORO_YIELD_VARARG(__VA_ARGS__, ___SHUSO_IO_CORO_YIELD_1PLUS, ___SHUSO_IO_CORO_YIELD_1PLUS, ___SHUSO_IO_CORO_YIELD_1PLUS, ___SHUSO_IO_CORO_YIELD_1PLUS, ___SHUSO_IO_CORO_YIELD_1PLUS, ___SHUSO_IO_CORO_YIELD_1)(__VA_ARGS__)

#define ___SHUSO_IO_CORO_YIELD_VARARG(_1,_2,_3,_4,_5,_6,NAME,...) NAME

#define ___SHUSO_IO_CORO_YIELD_1PLUS(io_operation, ...) \
    ___handler_io_struct->handler_stage = __LINE__; \
    shuso_io_ ## io_operation (___handler_io_struct, __VA_ARGS__); \
    return; \
  case __LINE__:

#define ___SHUSO_IO_CORO_YIELD_1(io_operation) \
    ___handler_io_struct->handler_stage = __LINE__; \
    shuso_io_ ## io_operation (___handler_io_struct); \
    return; \
  case __LINE__:


#define ___SHUSO_IO_CORO_BEGIN_VARARG(_1,_2,NAME,...) NAME
#define SHUSO_IO_CORO_BEGIN(...) ___SHUSO_IO_CORO_BEGIN_VARARG(__VA_ARGS__, SHUSO_IO_CORO_BEGIN_2, SHUSO_IO_CORO_BEGIN_1)(__VA_ARGS__)

#define SHUSO_IO_CORO_BEGIN_2(io, errhandler) \
  io->error_handler = errhandler; \
  SHUSO_IO_CORO_BEGIN_1(io)

#ifdef SHUTTLESOCK_DEBUG_IO

#define SHUSO_IO_CORO_BEGIN_1(io) \
  io->runner.name = __FUNCTION__; \
  io->runner.file = __FILE__; \
  io->runner.line = __LINE__; \
  ___SHUSO_IO_CORO_BEGIN(io)

#else
  
#define SHUSO_IO_CORO_BEGIN_1(io) \
  ___SHUSO_IO_CORO_BEGIN(io)

#endif

#define SHUSO_IO_CORO_END(io) \
} \
assert(io == ___handler_io_struct); \
___handler_io_struct->handler_stage = 0 \
  




#endif // SHUTTLESOCK_IO_H
