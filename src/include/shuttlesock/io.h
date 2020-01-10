#ifndef SHUTTLESOCK_IO_H
#define SHUTTLESOCK_IO_H

#include <shuttlesock/common.h>
#include <sys/uio.h>
#include <sys/socket.h>

typedef struct shuso_io_s shuso_io_t;

typedef void shuso_io_fn(shuso_t *S, shuso_io_t *io, int error);

struct shuso_io_s {
  union {
    struct msghdr  *msg;
    struct iovec   *iov;
    const char     *buf;
  };
  union {
    int     iovcnt;
    size_t  len;
    int     flags;
  };
  
  void             *pd;
  
  //everything else is private, more or less
  shuso_t          *S;
  int               fd;
  int               coroutine_stage;
  
  shuso_io_fn      *callback;
  
  int               watching_events; //read (EV_READ), write (EV_WRITE) or none
  shuso_ev_io       watcher;
  void            (*op_handler)(shuso_io_t *, int evflags);
};


void shuso_io_init_fd(shuso_t *S, shuso_io_t *io, int fd);
void shuso_io_init_socket(shuso_t *S, shuso_io_t *io, shuso_socket_t *socket);
void shuso_io_set_callback(shuso_io_t *io, shuso_io_fn *cb);
void shuso_io_set_privdata(shuso_io_t *io, void *pd);

void shuso_io_writev_complete(shuso_io_t *io, struct iovec *iov, int iovcnt);
void shuso_io_readv_complete(shuso_io_t *io, struct iovec *iov, int iovcnt);

void shuso_io_write(shuso_io_t *io, const void *buf, size_t len);
void shuso_io_read(shuso_io_t *io, void *buf, size_t len);

void shuso_io_connect(shuso_io_t *io, const shuso_socket_t *socket);
void shuso_io_accept(shuso_io_t *io, shuso_socket_t *socket);

void shuso_io_sendmsg(shuso_io_t *io, const struct msghdr *msg, int flags);
void shuso_io_recvmsg(shuso_io_t *io, struct msghdr *msg, int flags);

void shuso_io_close(shuso_io_t *io);

#define SHUSO_IO_CORO_BEGIN(io) \
  shuso_io_t *___coroutine_io_struct = io; \
  switch(io->coroutine_stage) { \
    case 0: {
      
#define SHUSO_IO_CORO_YIELD(io_call) \
      ___coroutine_io_struct->coroutine_stage = __LINE__; \
      io_call; \
      return; \
    } \
    case __LINE__: {

#define SHUSO_IO_CORO_END \
    } \
  }

#endif // SHUTTLESOCK_IO_H
