#ifndef SHUTTLESOCK_IO_LIBEV_H
#include <shuttlesock/common.h>

void shuso_io_ev_operation(shuso_io_t *io);

void shuso_io_ev_watch_update(shuso_io_t *io);

void shuso_io_ev_init_socket(shuso_t *S, shuso_io_t *io, shuso_socket_t *sock, int readwrite, shuso_io_fn *coro, void *privdata);
#endif
