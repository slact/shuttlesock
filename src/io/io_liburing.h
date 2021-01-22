#ifndef SHUTTLESOCK_IO_LIBURING_H
#define SHUTTLESOCK_IO_LIBURING_H

#include <shuttlesock/common.h>

#ifdef SHUTTLESOCK_HAVE_IO_URING
void shuso_io_uring_watch_update(shuso_io_t *io);
void shuso_io_uring_init_socket(shuso_t *S, shuso_io_t *io, shuso_socket_t *sock, int readwrite, shuso_io_fn *coro, void *privdata);
void shuso_io_uring_operation(shuso_io_t *io);
#else
#define shuso_io_uring_watch_update(...)
#define shuso_io_uring_init_socket(...)
#define shuso_io_uring_operation(...)
#endif

#endif //SHUTTLESOCK_IO_LIBURING_H
