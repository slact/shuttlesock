#include <shuttlesock.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef SHUTTLESOCK_HAVE_IO_URING
#include <liburing.h>
#endif

#define log_io_debug(io, fmt, ...) \
  shuso_log_debug(io->S, "io %p fd %d op %s w %s: " fmt, (void *)(io), (io)->fd, io_op_str((io)->op_code), io_watch_type_str((io)->watch_type), __VA_ARGS__)

static void shuso_io_ev_operation(shuso_io_t *io);
static void io_coro_run(shuso_io_t *io);
static void shuso_io_operation(shuso_io_t *io);

bool shuso_io_coro_fresh(shuso_io_t *io) {
  return io->coroutine_stage == 0 && io->watch_type == SHUSO_IO_WATCH_NONE;
}
/*
static char *shuso_io_op_str(shuso_io_opcode_t op) {
  switch(op) {
    case SHUSO_IO_OP_NONE:    return "none";
    case SHUSO_IO_OP_READV:   return "readv";
    case SHUSO_IO_OP_WRITEV:  return "writev";
    case SHUSO_IO_OP_READ:    return "read";
    case SHUSO_IO_OP_WRITE:   return "write";
    case SHUSO_IO_OP_SENDMSG: return "sendmsg";
    case SHUSO_IO_OP_RECVMSG: return "recvmsg";
  }
  return "???";
}
*/

/*
static char *shuso_io_watch_type_str(shuso_io_watch_type_t watchtype) {
  switch(watchtype) {
    case SHUSO_IO_WATCH_NONE:       return "none";
    case SHUSO_IO_WATCH_OP_FINISH:  return "op_finish";
    case SHUSO_IO_WATCH_OP_RETRY:   return "op_retry";
    case SHUSO_IO_WATCH_POLL_READ:  return "poll_read";
    case SHUSO_IO_WATCH_POLL_WRITE: return "poll_write";
    case SHUSO_IO_WATCH_POLL_READWRITE: return "poll_readwrite";
  }
  return "???";
}
*/

static void io_ev_watcher_handler(shuso_loop *loop, shuso_ev_io *ev, int evflags);

void __shuso_io_coro_init(shuso_t *S, shuso_io_t *io, int fd, int readwrite, shuso_io_fn *coro, void *privdata) {
  assert(readwrite == SHUSO_IO_READ || readwrite == SHUSO_IO_WRITE || readwrite == (SHUSO_IO_READ | SHUSO_IO_WRITE));
  *io = (shuso_io_t ){
    .S = S,
#ifdef SHUTTLESOCK_HAVE_IO_URING
    .use_io_uring = S->io_uring.on,
#else
    .use_io_uring = 0,
#endif
    .result = 0,
    .privdata = privdata,
    .coroutine_stage = 0,
    .coroutine = coro,
    .fd = fd,
    .watch_type = SHUSO_IO_WATCH_NONE,
    .error = 0,
    .readwrite = readwrite
  };
  
  if(io->use_io_uring) {
    //TODO: initialize io_uring fd watcher
  }
  else {
    int events = 0;
    if(io->readwrite & SHUSO_IO_READ) {
      events |= SHUSO_IO_READ;
    }
    if(io->readwrite & SHUSO_IO_WRITE) {
      events |= SHUSO_IO_WRITE;
    }
    shuso_ev_init(io->S, &io->watcher, io->fd, events, io_ev_watcher_handler, io);
  }
}

static bool ev_opcode_match_event_type(shuso_io_opcode_t opcode, int evflags) {
  switch(opcode) {
    case SHUSO_IO_OP_NONE:
      //should not happen
      raise(SIGABRT);
      return false;
      
    case SHUSO_IO_OP_READV:
    case SHUSO_IO_OP_READ:
    case SHUSO_IO_OP_RECVMSG:
      return evflags & EV_READ;
    
    case SHUSO_IO_OP_WRITEV:
    case SHUSO_IO_OP_WRITE:
    case SHUSO_IO_OP_SENDMSG:
      return evflags & EV_WRITE;
  }
  return false;
}

static bool ev_watch_poll_match_event_type(shuso_io_watch_type_t watchtype, int evflags) {
  if(evflags & EV_READ) {
    return watchtype == SHUSO_IO_WATCH_POLL_READ || watchtype == SHUSO_IO_WATCH_POLL_READWRITE;
  }
  if(evflags & EV_WRITE) {
    return watchtype == SHUSO_IO_WATCH_POLL_WRITE || watchtype == SHUSO_IO_WATCH_POLL_READWRITE;
  }
  return false;
}

static void io_ev_watcher_handler(shuso_loop *loop, shuso_ev_io *ev, int evflags) {
  shuso_io_t *io = shuso_ev_data(ev);
  
  switch(io->watch_type) {    
    case SHUSO_IO_WATCH_NONE:
      //should never happer
      raise(SIGABRT);
      break;
    
    case SHUSO_IO_WATCH_OP_FINISH:
      raise(SIGABRT); //should never happen using libev
      break;
      
    case SHUSO_IO_WATCH_OP_RETRY:
      if(ev_opcode_match_event_type(io->op_code, evflags)) {
        io->watch_type = SHUSO_IO_WATCH_NONE;
        assert(io->op_code != SHUSO_IO_OP_NONE);
        shuso_io_operation(io);
      }
      break;
    
    case SHUSO_IO_WATCH_POLL_READ:
    case SHUSO_IO_WATCH_POLL_WRITE:
    case SHUSO_IO_WATCH_POLL_READWRITE:
      if(ev_watch_poll_match_event_type(io->watch_type, evflags)) {
        io->watch_type = SHUSO_IO_WATCH_NONE;
        io_coro_run(io);
      }
      break;
  }
}

static void io_watch_update(shuso_io_t *io) {
#ifdef SHUTTLESOCK_HAVE_IO_URING
  if(io->use_io_uring) {
    //TODO: //run watcher or op via io_uring
    return;
  }
#endif
  bool ev_watcher_active = shuso_ev_active(&io->watcher);
  if(io->watch_type == SHUSO_IO_WATCH_NONE && ev_watcher_active) {
    //watcher needs to be stopped
    shuso_ev_stop(io->S, &io->watcher);
  }
  else if(io->watch_type != SHUSO_IO_WATCH_NONE && !ev_watcher_active) {
    //watcher needs to be started
    shuso_ev_start(io->S, &io->watcher);
  }
}

static void io_coro_run(shuso_io_t *io) {
  io->coroutine(io->S, io);
  io_watch_update(io);
}

void shuso_io_coro_resume_buf(shuso_io_t *io, char *buf, size_t len) {
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

/*
static size_t iovec_size(const struct iovec *iov, size_t iovcnt) {
  size_t sz = 0;
  for(size_t i=0; i<iovcnt; i++) {
    sz += iov->iov_len;
  }
  return sz;
}
*/

static void iovec_update_after_incomplete_operation(struct iovec **iov_ptr, size_t *iovcnt_ptr, ssize_t written) {
  size_t         iovcnt = *iovcnt_ptr;
  size_t         remaining = written;
  struct iovec  *iov = *iov_ptr;
  for(size_t i=0; i<iovcnt; i++) {
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
  io_watch_update(io);
}

static void io_update_incomplete_op_data(shuso_io_t *io, ssize_t result_sz) {
  assert(result_sz >= 0);
  switch((shuso_io_opcode_t )io->op_code) {
      case SHUSO_IO_OP_NONE:
        return;
        
      case SHUSO_IO_OP_READ:
      case SHUSO_IO_OP_WRITE:
        io->buf = &io->buf[result_sz];
        io->len -= result_sz;
        return;
        
      case SHUSO_IO_OP_READV:
      case SHUSO_IO_OP_WRITEV:
        iovec_update_after_incomplete_operation(&io->iov, &io->iovcnt, result_sz);
        return;
        
      case SHUSO_IO_OP_RECVMSG:
      case SHUSO_IO_OP_SENDMSG:
        iovec_update_after_incomplete_operation(&io->msg->msg_iov, &io->msg->msg_iovlen, result_sz);
        return;
    }
}

static void shuso_io_ev_operation(shuso_io_t *io) {
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
        assert(io->len > 0);
        result_sz = write(io->fd, io->buf, io->len);
        break;
      case SHUSO_IO_OP_READV:
        result_sz = readv(io->fd, io->iov, io->iovcnt);
        break;
      case SHUSO_IO_OP_WRITEV:
        result_sz = writev(io->fd, io->iov, io->iovcnt);
        break;
      case SHUSO_IO_OP_RECVMSG:
        result_sz = recvmsg(io->fd, io->msg, io->flags);
        break;
      case SHUSO_IO_OP_SENDMSG:
        result_sz = sendmsg(io->fd, io->msg, io->flags);
        break;
    }
  } while(result_sz == -1 && errno == EINTR);
  if(result_sz == -1 && (errno == EAGAIN || errno == EWOULDBLOCK) && io->op_repeat_to_completion) {
    if(result_sz == -1) {
      result_sz = 0; //-1 means zero bytes processed, in case of EAGAIN, right?
    }
    io->result += result_sz;
    io_update_incomplete_op_data(io, result_sz);
    assert(io->len > 0);
    retry_ev_operation(io);
    return;
  }
  
  if(result_sz == -1) {
    io->result = -1;
    //legit error happened
    io->result = -1;
    io->error = errno;
    io->op_code = SHUSO_IO_OP_NONE;
    io_coro_run(io);
    return;
  }
  
  io->result += result_sz;
  io->op_code = SHUSO_IO_OP_NONE;
  io_coro_run(io);
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
  if(io->use_io_uring) {
    //do io_uring stuff
  }
  else {
    shuso_io_ev_operation(io);
  }
}

static void io_op_run_new(shuso_io_t *io, int opcode, void *init_buf, ssize_t init_len, bool partial, bool registered) {
  io->result = 0;
  io->error = 0;
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

void shuso_io_coro_stop(shuso_io_t *io) {
  switch(io->watch_type) {
    case SHUSO_IO_WATCH_NONE:
      break;
    case SHUSO_IO_WATCH_OP_FINISH:
    case SHUSO_IO_WATCH_OP_RETRY:
    case SHUSO_IO_WATCH_POLL_READ:
    case SHUSO_IO_WATCH_POLL_WRITE:
    case SHUSO_IO_WATCH_POLL_READWRITE:
      io->watch_type = SHUSO_IO_WATCH_NONE;
      io->result = -1;
      io->error = ECANCELED;
      io->coroutine(io->S, io);
  }
  shuso_io_coro_abort(io);
}

void shuso_io_coro_abort(shuso_io_t *io) {
  io->watch_type = SHUSO_IO_WATCH_NONE;
  io->error = ECANCELED;
  io->result = -1;
  io_watch_update(io);
  //stop it at once!
}

void shuso_io_wait(shuso_io_t *io, int evflags) {
  switch(evflags) {
    case SHUSO_IO_READ | SHUSO_IO_WRITE :
      io->watch_type = SHUSO_IO_WATCH_POLL_READWRITE;
      break;
    case SHUSO_IO_READ:
      io->watch_type = SHUSO_IO_WATCH_POLL_READ;
      break;
    case SHUSO_IO_WRITE:
      io->watch_type = SHUSO_IO_WATCH_POLL_WRITE;
      break;
  }
  io->error = 0;
  io->result = 0;
  io_watch_update(io);
}

void shuso_io_writev_partial(shuso_io_t *io, struct iovec *iov, int iovcnt) {
  io_op_run_new(io, SHUSO_IO_OP_WRITEV, iov, iovcnt, true, false);
}
void shuso_io_writev(shuso_io_t *io, struct iovec *iov, int iovcnt) {
  io_op_run_new(io, SHUSO_IO_OP_WRITEV, iov, iovcnt, false, false);
}
void shuso_io_readv_partial(shuso_io_t *io, struct iovec *iov, int iovcnt) {
  io_op_run_new(io, SHUSO_IO_OP_READV, iov, iovcnt, true, false);
}
void shuso_io_readv(shuso_io_t *io, struct iovec *iov, int iovcnt) {
  io_op_run_new(io, SHUSO_IO_OP_READV, iov, iovcnt, false, false);
}

void shuso_io_write_partial(shuso_io_t *io, const void *buf, size_t len) {
  io_op_run_new(io, SHUSO_IO_OP_WRITE, (void *)buf, len, true, false);
}
void shuso_io_write(shuso_io_t *io, const void *buf, size_t len) {
  io_op_run_new(io, SHUSO_IO_OP_WRITE, (void *)buf, len, false, false);
}
void shuso_io_read_partial(shuso_io_t *io, void *buf, size_t len) {
  io_op_run_new(io, SHUSO_IO_OP_READ, buf, len, true, false);
}
void shuso_io_read(shuso_io_t *io, void *buf, size_t len) {
  io_op_run_new(io, SHUSO_IO_OP_READ, buf, len, false, false);
}

void shuso_io_sendmsg(shuso_io_t *io, struct msghdr *msg, int flags) {
  io_op_run_new(io, SHUSO_IO_OP_SENDMSG, msg, flags, false, false);
}
void shuso_io_recvmsg(shuso_io_t *io, struct msghdr *msg, int flags) {
  io_op_run_new(io, SHUSO_IO_OP_RECVMSG, msg, flags, false, false);
}

