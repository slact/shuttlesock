#ifndef SHUTTLESOCK_IO_LIBURING_H
#include <shuttlesock/common.h>

void shuso_io_uring_watch_update(shuso_io_t *io);
void shuso_io_uring_init_socket(shuso_t *S, shuso_io_t *io, shuso_socket_t *sock, int readwrite, shuso_io_fn *coro, void *privdata);
void shuso_io_uring_operation(shuso_io_t *io);
#endif
