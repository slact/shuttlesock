#include <shuttlesock.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef SHUTTLESOCK_HAVE_IO_URING
#include <liburing.h>
#endif

static void shuso_io_ev_operation(shuso_io_t *io);
static void io_coro_run(shuso_io_t *io);

bool shuso_io_coro_fresh(shuso_io_t *io) {
  return io->coroutine_stage && !io->busy;
}

static void io_ev_watcher_handler(shuso_loop *loop, shuso_ev_io *ev, int evflags);

void __shuso_io_coro_init(shuso_t *S, shuso_io_t *io, int fd, int readwrite, shuso_io_fn *coro, void *privdata) {
  *io = (shuso_io_t ){
    .S = S,
#ifdef SHUTTLESOCK_HAVE_IO_URING
    .use_io_uring = S->io_uring.on,
#endif
    .result = 0,
    .privdata = privdata,
    .busy = false,
    .coroutine_stage = 0,
    .callback = coro,
    .fd = fd,
    .watch_type = SHUSO_IO_WATCH_NONE,
    .error = 0,
    .readwrite = readwrite
  };
  
#ifdef SHUTTLESOCK_HAVE_IO_URING
  if(io->use_io_uring) {
    //TODO: initialize io_uring fd watcher
    return;
  }
#endif

  int events = 0;
  if(io->readwrite & SHUSO_IO_READ) {
    events |= SHUSO_IO_READ;
  }
  if(io->readwrite & SHUSO_IO_WRITE) {
    events |= SHUSO_IO_WRITE;
  }
  shuso_ev_init(io->S, &io->watcher, io->fd, events, io_ev_watcher_handler, io);
  
}

static void io_ev_watcher_handler(shuso_loop *loop, shuso_ev_io *ev, int evflags) {
  shuso_io_t *io = shuso_ev_data(ev);
  switch(io->watch_type) {
    
    case SHUSO_IO_WATCH_NONE:
      //this should never happen
      raise(SIGABRT);
      break;
    
    case SHUSO_IO_WATCH_OP_FINISH:
    case SHUSO_IO_WATCH_OP_RETRY:
      switch((shuso_io_opcode_t )io->op_code) {
        case SHUSO_IO_OP_NONE:
          //this should never happen
          raise(SIGABRT);
          break;
          
        case SHUSO_IO_OP_READV:
        case SHUSO_IO_OP_READ:
        case SHUSO_IO_OP_RECVMSG:
          if(evflags & EV_READ) {
            io->watch_type = SHUSO_IO_WATCH_NONE;
            shuso_io_ev_operation(io);
          }
          break;
        case SHUSO_IO_OP_WRITEV:
        case SHUSO_IO_OP_WRITE:
        case SHUSO_IO_OP_SENDMSG:
          if(evflags & EV_WRITE) {
            io->watch_type = SHUSO_IO_WATCH_NONE;
            shuso_io_ev_operation(io);
          }
          break;
      }
      break;
    
    case SHUSO_IO_WATCH_POLL_READ:
      if(evflags & EV_READ) {
        io->watch_type = SHUSO_IO_WATCH_NONE;
        io_coro_run(io);
      }
      break;
    
    case SHUSO_IO_WATCH_POLL_WRITE:
      if(evflags & EV_WRITE) {
        io->watch_type = SHUSO_IO_WATCH_NONE;
        io_coro_run(io);
      }
      break;
    
    case SHUSO_IO_WATCH_POLL_READWRITE:
      io->watch_type = SHUSO_IO_WATCH_NONE;
      io_coro_run(io);
      break;
  }
}

static void io_coro_run(shuso_io_t *io) {
  io->callback(io->S, io);
  if(io->watch_type != SHUSO_IO_WATCH_NONE) {
#ifdef SHUTTLESOCK_HAVE_IO_URING
    if(io->use_io_uring) {
      //TODO: run io_uring fd poll maybe?
      return;
    }
#endif
    //libev fd poll watcher
    if(!shuso_ev_active(&io->watcher)) {
      shuso_ev_start(io->S, &io->watcher);
    }
  }
}

void shuso_io_coro_resume_data(shuso_io_t *io, char *buf, size_t len) {
  io->buf = buf;
  io->len = len;
  io_coro_run(io);
}
void shuso_io_coro_resume_iovec(shuso_io_t *io, struct iovec *iov, int iovcnt) {
  io->iov = iov;
  io->iovcnt = iovcnt;
  io_coro_run(io);
}
void shuso_io_coro_resume_msg(shuso_io_t *io, struct msghdr *msg, int flags) {
  io->msg = msg;
  io->flags = flags;
  io_coro_run(io);
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

static void retry_ev_operation(shuso_io_t *io) {
  assert(io->watch_type == SHUSO_IO_WATCH_NONE);
  io->watch_type = SHUSO_IO_WATCH_OP_RETRY;
}

static void shuso_io_ev_operation(shuso_io_t *io) {
  ssize_t required_sz;
  if(io->op_repeat_to_completion) {
    required_sz = io->op_io_vector ? iovec_size(io->iov, io->iovcnt) : io->len;
  }
  
  ssize_t result_sz;
  do {
    switch((shuso_io_opcode_t )io->op_code) {
      case SHUSO_IO_OP_NONE:
        //should never happen
        raise(SIGABRT);
        break;
        
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
    io->result = -1;
    io->error = errno;
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
#ifdef SHUTTLESOCK_DEBUG_IO
  switch((shuso_io_opcode_t )io->op_code) {
    case SHUSO_IO_OP_READV:
    case SHUSO_IO_OP_READ:
    case SHUSO_IO_OP_RECVMSG:
      assert(io->readwrite & SHUSO_IO_READ);
      break;
    case SHUSO_IO_OP_WRITEV:
    case SHUSO_IO_OP_WRITE:
    case SHUSO_IO_OP_SENDMSG:
      assert(io->readwrite & SHUSO_IO_WRITE);
      break;
  }
#endif
#ifdef SHUTTLESOCK_HAVE_IO_URING
  if(io->use_io_uring) {
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

