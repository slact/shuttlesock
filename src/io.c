#include <shuttlesock.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef SHUTTLESOCK_HAVE_IO_URING
#include <liburing.h>
#endif

static size_t iovec_size(const struct iovec *iov, int iovcnt) {
  size_t sz = 0;
  for(int i=0; i<iovcnt; i++) {
    sz += iov->iov_len;
  }
  return sz;
}
static void iovec_update_after_incomplete_write(struct iovec **iov_ptr, int *iovcnt_ptr, ssize_t written) {
  int            iovcnt = *iovcnt_ptr;
  size_t         remaining = written;
  struct iovec  *iov = *iov_ptr;
  for(int i=0; i<iovcnt; i++) {
    if(remaining < iov[i].iov_len) {
      *iov_ptr = &iov[i];
      iov[i].iov_len -= remaining;
      *iovcnt_ptr = iovcnt - i;
      return;
    }
  }
  //this should not happen as long as the number of bytes written < size(iovec_data);
  abort();
}

static void io_watcher_handler(shuso_loop *loop, shuso_ev_io *ev, int evflags);
static void io_watch(shuso_io_t *io, int direction, void (*handler)(shuso_io_t *, int evflags)) {
  assert(!io->op_handler);
#ifdef SHUTTLESOCK_HAVE_IO_URING
  if(io->S->io_uring.on) {
    // io_uring stuff;
    return;
  }
#endif
  //libev
  assert((direction & EV_READ) || (direction & EV_WRITE));
  shuso_ev_io_init(io->S, &io->watcher, io->fd, direction, io_watcher_handler, io);
  io->op_handler = handler;
}

static void io_watcher_handler(shuso_loop *loop, shuso_ev_io *ev, int evflags) {
  shuso_io_t *io = shuso_ev_data(ev);
  if(!(io->watching_events & evflags)) {
    return;
  }
  void (*handler)(shuso_io_t *, int evflags) = io->op_handler;
  io->op_handler = NULL;
  handler(io, evflags);
}

static void io_writev_complete_watcher_handler(shuso_io_t *io, int evflags) {
  assert(evflags & EV_WRITE);
  shuso_io_writev_complete(io, io->iov, io->iovcnt);
}

void shuso_io_writev_complete(shuso_io_t *io, struct iovec *iov, int iovcnt) {
  shuso_t *S = io->S;
#ifdef SHUTTLESOCK_HAVE_IO_URING
  if(S->io_uring.on) {
    //do io_uring stuff
    
    return;
  }
#endif
  //libev fallback
  ssize_t iov_sz = iovec_size(iov, iovcnt);
  ssize_t sent;
  do {
    sent = writev(io->fd, iov, iovcnt);
  } while(sent == -1 && errno == EINTR);
  if(sent == -1) {
    if(errno == EAGAIN || errno == EWOULDBLOCK) {
      //try again later
      goto retry_later;
    }
    else {
      //legit error happened
      io->callback(io->S, io, errno);
    }
    return;
  }
  else if(sent < iov_sz) {
    iovec_update_after_incomplete_write(&iov, &iovcnt, sent);
    goto retry_later;
  }
  io->callback(io->S, io, 0);
  return;
  
retry_later:
  io->iov = iov;
  io->iovcnt = iovcnt;
  io_watch(io, EV_WRITE, io_writev_complete_watcher_handler);
}


void shuso_io_readv_complete(shuso_io_t *io, struct iovec *iov, int iovcnt) {
  shuso_t *S = io->S;
#ifdef SHUTTLESOCK_HAVE_IO_URING
  if(S->io_uring.on) {
    //do io_uring stuff
    return;
  }
#endif
    //libev fallback
  ssize_t iov_sz = iovec_size(iov, iovcnt);
  ssize_t read_sz;
  do {
    read_sz = readv(io->fd, iov, iovcnt);
  } while(read_sz == -1 && errno == EINTR);
  if(read_sz == -1) {
    if(errno == EAGAIN || errno == EWOULDBLOCK) {
      //try again later
      goto retry_later;
    }
    else {
      //legit error happened
      io->callback(io->S, io, errno);
    }
    return;
  }
  else if(sent < iov_sz) {
    iovec_update_after_incomplete_write(&iov, &iovcnt, sent);
    goto retry_later;
  }
  io->callback(io->S, io, 0);
  return;
  
retry_later:
  io->iov = iov;
  io->iovcnt = iovcnt;
  io_watch(io, EV_WRITE, io_writev_complete_watcher_handler);
}
}
