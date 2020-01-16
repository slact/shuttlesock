#include <shuttlesock.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef SHUTTLESOCK_HAVE_IO_URING
#include <liburing.h>
#endif

void shuso_io_coro_init(shuso_t *S, shuso_io_t *io, int fd, shuso_io_fn *coro, void *privdata) {
  *io = (shuso_io_t ){
    .S = S,
    .data = NULL,
    .int_data = 0,
    .result = 0,
    .privdata = privdata,
    .busy = false,
    .coroutine_stage = 0,
    .callback = coro,
    .fd = fd
  };
}

void shuso_io_coro_resume(shuso_io_t *io, void *data, int int_data) {
  io->data = data;
  io->int_data = int_data;
  io->callback(io->S, io);
}

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

static void io_ev_watcher_handler(shuso_loop *loop, shuso_ev_io *ev, int evflags);

static void io_ev_watch(shuso_io_t *io, int direction) {
  assert(!io->busy);
  io->busy = true;
  assert((direction & EV_READ) || (direction & EV_WRITE));
  shuso_ev_io_init(io->S, &io->watcher, io->fd, direction, io_ev_watcher_handler, io);
}

static void shuso_io_ev_operation(shuso_io_t *io);

static void io_ev_watcher_handler(shuso_loop *loop, shuso_ev_io *ev, int evflags) {
  shuso_io_t *io = shuso_ev_data(ev);
  assert(io->busy);
  io->busy = false;
  
  switch((shuso_io_opcode_t )io->op_code) {
    case SHUSO_IO_OP_READV:
    case SHUSO_IO_OP_READ:
    case SHUSO_IO_OP_RECVMSG:
      if(evflags & EV_READ) {
        shuso_io_ev_operation(io);
      }
      break;
    case SHUSO_IO_OP_WRITEV:
    case SHUSO_IO_OP_WRITE:
    case SHUSO_IO_OP_SENDMSG:
      if(evflags & EV_WRITE) {
        shuso_io_ev_operation(io);
      }
      break;
  }
}

static void retry_ev_operation(shuso_io_t *io) {
  int evflag;
  switch((shuso_io_opcode_t )io->op_code) {
    case SHUSO_IO_OP_READV:
    case SHUSO_IO_OP_READ:
    case SHUSO_IO_OP_RECVMSG:
      evflag = EV_READ;
      break;
    case SHUSO_IO_OP_WRITEV:
    case SHUSO_IO_OP_WRITE:
    case SHUSO_IO_OP_SENDMSG:
      evflag = EV_WRITE;
      break;
  }
  io_ev_watch(io, evflag);
}

static void shuso_io_ev_operation(shuso_io_t *io) {
  ssize_t required_sz;
  if(io->op_repeat_to_completion) {
    required_sz = io->op_io_vector ? iovec_size(io->iov, io->iovcnt) : io->len;
  }
  
  ssize_t result_sz;
  do {
    switch((shuso_io_opcode_t )io->op_code) {
      case SHUSO_IO_OP_READ:
        result_sz = read(io->fd, io->buf, io->len);
        break;
      case SHUSO_IO_OP_WRITE:
        result_sz = write(io->fd, io->buf, io->len);
        break;
      case SHUSO_IO_OP_READV:
        result_sz = readv(io->fd, io->iov, io->iovcnt);
        break;
      case SHUSO_IO_OP_WRITEV:
        result_sz = writev(io->fd, io->iov, io->iovcnt);
        break;
      case SHUSO_IO_OP_SENDMSG:
        result_sz = sendmsg(io->fd, io->msg, io->flags);
        break;
      case SHUSO_IO_OP_RECVMSG:
        result_sz = recvmsg(io->fd, io->msg, io->flags);
        break;
    }
  } while(result_sz == -1 && errno == EINTR);
  
  io->result += result_sz;
  
  if(result_sz == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    //try again later
    retry_ev_operation(io);
    return;
  }
  
  if(result_sz == -1) {
    //legit error happened
    io->result = errno;
    io->callback(io->S, io);
    return;
  }
  
  if(io->op_repeat_to_completion && result_sz < required_sz) {
    if(io->op_io_vector) {
      iovec_update_after_incomplete_write(&io->iov, &io->iovcnt, result_sz);
    }
    else {
      io->buf = &io->buf[result_sz];
      io->len -= result_sz;
    }
    
    retry_ev_operation(io);
    return;
  }
  io->result = 0;
  io->callback(io->S, io);
}

static void shuso_io_operation(shuso_io_t *io) {
#ifdef SHUTTLESOCK_HAVE_IO_URING
  if(io->S->io_uring.on) {
    //do io_uring stuff
    return;
  }
#endif
  shuso_io_ev_operation(io);
}

static void io_op_run(shuso_io_t *io, shuso_io_fn *cb, void *pd, int opcode, void *init_buf, ssize_t init_len, bool partial, bool registered) {
  assert(!io->busy);
  if(cb != SHUSO_IO_UNCHANGED_CALLBACK) {
    io->callback = cb;
  }
  if(pd != SHUSO_IO_UNCHANGED_PRIVDATA) {
    io->privdata = pd;
  }
  
  io->result = 0;
  io->op_code = opcode;
  if(opcode == SHUSO_IO_OP_READV || opcode == SHUSO_IO_OP_WRITEV) {
    io->op_io_vector = true;
    io->iov = init_buf;
    io->iovcnt = init_len;
  }
  else {
    io->op_io_vector = false;
    io->buf = init_buf;
    io->len = init_len;
  }
  io->op_repeat_to_completion = !partial;
  io->op_registered_memory_buffer = registered;
  
  shuso_io_operation(io);
}
  
void shuso_io_writev_partial(shuso_io_t *io, struct iovec *iov, int iovcnt, shuso_io_fn *cb, void *pd) {
  io_op_run(io, cb, pd, SHUSO_IO_OP_WRITEV, iov, iovcnt, true, false);
}
void shuso_io_writev(shuso_io_t *io, struct iovec *iov, int iovcnt, shuso_io_fn *cb, void *pd) {
  io_op_run(io, cb, pd, SHUSO_IO_OP_WRITEV, iov, iovcnt, false, false);
}
void shuso_io_readv_partial(shuso_io_t *io, struct iovec *iov, int iovcnt, shuso_io_fn *cb, void *pd) {
  io_op_run(io, cb, pd, SHUSO_IO_OP_READV, iov, iovcnt, true, false);
}
void shuso_io_readv(shuso_io_t *io, struct iovec *iov, int iovcnt, shuso_io_fn *cb, void *pd) {
  io_op_run(io, cb, pd, SHUSO_IO_OP_READV, iov, iovcnt, false, false);
}

void shuso_io_write_partial(shuso_io_t *io, const void *buf, size_t len, shuso_io_fn *cb, void *pd) {
  io_op_run(io, cb, pd, SHUSO_IO_OP_WRITE, (void *)buf, len, true, false);
}
void shuso_io_write(shuso_io_t *io, const void *buf, size_t len, shuso_io_fn *cb, void *pd) {
  io_op_run(io, cb, pd, SHUSO_IO_OP_WRITE, (void *)buf, len, false, false);
}
void shuso_io_read_partial(shuso_io_t *io, void *buf, size_t len, shuso_io_fn *cb, void *pd) {
  io_op_run(io, cb, pd, SHUSO_IO_OP_READ, buf, len, true, false);
}
void shuso_io_read(shuso_io_t *io, void *buf, size_t len, shuso_io_fn *cb, void *pd) {
  io_op_run(io, cb, pd, SHUSO_IO_OP_READ, buf, len, false, false);
}

void shuso_io_sendmsg(shuso_io_t *io, struct msghdr *msg, int flags, shuso_io_fn *cb, void *pd) {
  io_op_run(io, cb, pd, SHUSO_IO_OP_SENDMSG, msg, flags, false, false);
}
void shuso_io_recvmsg(shuso_io_t *io, struct msghdr *msg, int flags, shuso_io_fn *cb, void *pd) {
  io_op_run(io, cb, pd, SHUSO_IO_OP_RECVMSG, msg, flags, false, false);
}

