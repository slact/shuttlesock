#include <shuttlesock/build_config.h>
#ifdef SHUTTLESOCK_HAVE_ACCEPT4
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <shuttlesock.h>
#include <errno.h>
#include <sys/uio.h>
#ifdef SHUTTLESOCK_HAVE_IO_URING
#include <liburing.h>
#endif

#define log_io_debug(io, fmt, ...) \
  shuso_log_debug(io->S, "io %p fd %d op %s w %s: " fmt, (void *)(io), (io)->io_socket.fd, io_op_str((io)->opcode), io_watch_type_str((io)->watch_type), __VA_ARGS__)

static void shuso_io_ev_operation(shuso_io_t *io);
static void io_run(shuso_io_t *io);
static void shuso_io_operation(shuso_io_t *io);

/*
static char *shuso_io_op_str(shuso_io_opcode_t op) {
  switch(op) {
    case SHUSO_IO_OP_NONE:    return "none";
    case SHUSO_IO_OP_READV:   return "readv";
    case SHUSO_IO_OP_WRITEV:  return "writev";
    case SHUSO_IO_OP_READ:    returRn "read";
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

void __shuso_io_init_socket(shuso_t *S, shuso_io_t *io, shuso_socket_t *sock, int readwrite, shuso_io_fn *coro, void *privdata) {
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
    .handler_stage = 0,
    .handler = coro,
    .watch_type = SHUSO_IO_WATCH_NONE,
    .error = 0,
    .readwrite = readwrite
  };
  if(sock) {
    io->io_socket = *sock;
  }
  
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
    shuso_ev_init(io->S, &io->watcher, io->io_socket.fd, events, io_ev_watcher_handler, io);
  }
}
void __shuso_io_init_hostinfo(shuso_t *S, shuso_io_t *io, shuso_hostinfo_t *hostinfo, int readwrite, shuso_io_fn *coro, void *privdata) {
  shuso_socket_t sock = { .fd = -1 };
  if(hostinfo) {
    sock.host = *hostinfo;
  }
  __shuso_io_init_socket(S, io, &sock, readwrite, coro, privdata);
}

void __shuso_io_init_fd(shuso_t *S, shuso_io_t *io, int fd, int readwrite, shuso_io_fn *coro, void *privdata) {
  shuso_socket_t sock = { .fd = fd };
  __shuso_io_init_socket(S, io, &sock, readwrite, coro, privdata);
}


static bool ev_opcode_finish_match_event_type(shuso_io_opcode_t opcode, int evflags) {
  switch(opcode) {
    case SHUSO_IO_OP_NONE:
    case SHUSO_IO_OP_READV:
    case SHUSO_IO_OP_READ:
    case SHUSO_IO_OP_RECVMSG:
    case SHUSO_IO_OP_WRITEV:
    case SHUSO_IO_OP_WRITE:
    case SHUSO_IO_OP_SENDMSG:
    case SHUSO_IO_OP_ACCEPT:
    case SHUSO_IO_OP_CLOSE:
    case SHUSO_IO_OP_SHUTDOWN:
      //should not happen
      raise(SIGABRT);
      return false;
    case SHUSO_IO_OP_CONNECT:
      return evflags & EV_WRITE;
  }
  return false;
}

static bool ev_opcode_retry_match_event_type(shuso_io_opcode_t opcode, int evflags) {
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
      
    case SHUSO_IO_OP_ACCEPT:
    case SHUSO_IO_OP_CONNECT:
    case SHUSO_IO_OP_CLOSE:
    case SHUSO_IO_OP_SHUTDOWN:
      return true;
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
  switch((shuso_io_watch_type_t )io->watch_type) {    
    case SHUSO_IO_WATCH_NONE:
      //should never happer
      raise(SIGABRT);
      break;
    
    case SHUSO_IO_WATCH_OP_FINISH:
      if(ev_opcode_finish_match_event_type(io->opcode, evflags)) {
        shuso_io_operation(io);
      }
      break;
      
    case SHUSO_IO_WATCH_OP_RETRY:
      if(ev_opcode_retry_match_event_type(io->opcode, evflags)) {
        io->watch_type = SHUSO_IO_WATCH_NONE;
        assert(io->opcode != SHUSO_IO_OP_NONE);
        shuso_io_operation(io);
      }
      break;
    
    case SHUSO_IO_WATCH_POLL_READ:
    case SHUSO_IO_WATCH_POLL_WRITE:
    case SHUSO_IO_WATCH_POLL_READWRITE:
      if(ev_watch_poll_match_event_type(io->watch_type, evflags)) {
        io->watch_type = SHUSO_IO_WATCH_NONE;
        io_run(io);
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

static void io_run(shuso_io_t *io) {
  if(io->error_handler && io->result == -1) {
    io->handler_stage = 0;
    io->error_handler(io->S, io);
  }
  else {
    io->handler(io->S, io);
    io_watch_update(io);
  }
}

void shuso_io_resume_buf(shuso_io_t *io, char *buf, size_t len) {
  io->buf = buf;
  io->len = len;
  io_run(io);
}
void shuso_io_resume_iovec(shuso_io_t *io, struct iovec *iov, int iovcnt) {
  io->iov = iov;
  io->iovcnt = iovcnt;
  io_run(io);
}
void shuso_io_resume_msg(shuso_io_t *io, struct msghdr *msg, int flags) {
  io->msg = msg;
  io->flags = flags;
  io_run(io);
}

void shuso_io_resume_data(shuso_io_t *io, void *data, int intdata) {
  io->result_data = data;
  io->intdata = intdata;
  io_run(io);
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

static bool io_iovec_op_update_and_check_completion(struct iovec **iov_ptr, size_t *iovcnt_ptr, ssize_t written) {
  //returns true if operation is complete (iovec now empty)
  size_t         iovcnt = *iovcnt_ptr;
  size_t         chomped = written;
  struct iovec  *iov = *iov_ptr;
  size_t         i;
  for(i=0; i < iovcnt; i++) {
    if(chomped < iov[i].iov_len) {
      *iov_ptr = &iov[i];
      iov[i].iov_len -= chomped;
      *iovcnt_ptr = iovcnt - i;
      return false;
    }
    else {
      assert(chomped >= iov[i].iov_len);
      chomped -= iov[i].iov_len;
    }
  }
  *iovcnt_ptr = 0;
  return true;
}

static bool io_op_update_and_check_completion(shuso_io_t *io, ssize_t result_sz) {
  switch((shuso_io_opcode_t )io->opcode) {
    case SHUSO_IO_OP_NONE:
    case SHUSO_IO_OP_CONNECT:
    case SHUSO_IO_OP_ACCEPT:
    case SHUSO_IO_OP_CLOSE:
    case SHUSO_IO_OP_SHUTDOWN:
      return true;
      
    case SHUSO_IO_OP_READ:
    case SHUSO_IO_OP_WRITE:
    case SHUSO_IO_OP_SENDTO:
    case SHUSO_IO_OP_RECVFROM:
      io->buf = &io->buf[result_sz];
      io->len -= result_sz;
      assert(io->len >= 0);
      return io->len == 0;
      
    case SHUSO_IO_OP_READV:
    case SHUSO_IO_OP_WRITEV:
      return io_iovec_op_update_and_check_completion(&io->iov, &io->iovcnt, result_sz);
      
    case SHUSO_IO_OP_RECVMSG:
    case SHUSO_IO_OP_SENDMSG:
      return io_iovec_op_update_and_check_completion(&io->msg->msg_iov, &io->msg->msg_iovlen, result_sz);
  }
  return true;
}

static int io_ev_connect(shuso_io_t *io) {
  if(io->io_socket.fd == -1) {
    errno = EBADF;
    return -1;
  }
  
  if(io->watch_type == SHUSO_IO_WATCH_OP_FINISH) {
    int so_val = 0;
    socklen_t so_val_sz = sizeof(so_val);
    int rc = getsockopt(io->io_socket.fd, SOL_SOCKET, SO_ERROR, &so_val, &so_val_sz);
    if(rc == 0 && so_val != 0) {
      errno = so_val;
      rc = -1;
    } 
    io->watch_type = SHUSO_IO_WATCH_NONE;
    io_watch_update(io);
    return rc;
  }
  
  struct sockaddr      *connect_sockaddr;
  sa_family_t           fam = 0;
  shuso_hostinfo_t     *host;
  size_t                sockaddr_sz;
  union {
    struct sockaddr        sa;
    struct sockaddr_un     sa_unix;
    struct sockaddr_in     sa_inet;
#ifdef SHUTTLESOCK_HAVE_IPV6
    struct sockaddr_in6    sa_inet6;
#endif
  }                      sockaddr_buf;
  
  if(io->io_socket.host.sockaddr) {
    assert(io->io_socket.host.addr_family == io->io_socket.host.sockaddr->sa_family);
    connect_sockaddr = io->io_socket.host.sockaddr;
    fam = io->io_socket.host.addr_family;
  }
  else {
    host = io->hostinfo;
    if(host->sockaddr) {
      connect_sockaddr = host->sockaddr;
      fam = io->io_socket.host.addr_family;
    }
    else {
      if(!shuso_hostinfo_to_sockaddr(io->S, host, &sockaddr_buf.sa, &sockaddr_sz)) {
        return -1;
      }
      connect_sockaddr = &sockaddr_buf.sa;
      fam = sockaddr_buf.sa.sa_family;
    }
  }
  switch(fam) {
    case AF_INET:
      sockaddr_sz = sizeof(struct sockaddr_in);
      break;
#ifdef SHUTTLESOCK_HAVE_IPV6
    case AF_INET6:
      sockaddr_sz = sizeof(struct sockaddr_in6);
      break;
#endif
    case AF_UNIX:
      sockaddr_sz = sizeof(struct sockaddr_un);
      break;
    default:
      sockaddr_sz = 0;
  }
  
  return connect(io->io_socket.fd, connect_sockaddr, sockaddr_sz);
}

static void shuso_io_ev_operation(shuso_io_t *io) {
  ssize_t result;
  shuso_io_opcode_t op = io->opcode;
  
  do {
    switch(op) {
      case SHUSO_IO_OP_NONE:
        //should never happen
        raise(SIGABRT);
        break;
        
      case SHUSO_IO_OP_READ:
        result = read(io->io_socket.fd, io->buf, io->len);
        break;
      case SHUSO_IO_OP_WRITE:
        assert(io->len > 0);
        result = write(io->io_socket.fd, io->buf, io->len);
        break;
      case SHUSO_IO_OP_READV:
        result = readv(io->io_socket.fd, io->iov, io->iovcnt);
        break;
      case SHUSO_IO_OP_WRITEV:
        result = writev(io->io_socket.fd, io->iov, io->iovcnt);
        break;
      case SHUSO_IO_OP_RECVMSG:
        result = recvmsg(io->io_socket.fd, io->msg, io->flags);
        break;
      case SHUSO_IO_OP_SENDMSG:
        result = sendmsg(io->io_socket.fd, io->msg, io->flags);
        break;
      case SHUSO_IO_OP_CONNECT:
        result = io_ev_connect(io);
        break;
      case SHUSO_IO_OP_ACCEPT: {
        socklen_t         socklen = io->len;
        assert(socklen == sizeof(*io->sockaddr));
        assert(io->sockaddr != NULL);
#ifdef SHUTTLESOCK_HAVE_ACCEPT4
        result = accept4(io->io_socket.fd, &io->sockaddr->any, &socklen, SOCK_NONBLOCK);
#else
        result = accept(io->io_socket.fd, &io->sockaddr->any, &socklen);
        if(result != -1) {
          fcntl(result, F_SETFL, O_NONBLOCK);
        }
#endif
        io->len = socklen;
        break;
      }
      case SHUSO_IO_OP_CLOSE:
        result = close(io->io_socket.fd);
        break;
      case SHUSO_IO_OP_SHUTDOWN:
        result = shutdown(io->io_socket.fd, io->flags);
        break;
    }
  } while(result == -1 && errno == EINTR);
  
  if(result == -1) {
    if(op == SHUSO_IO_OP_CONNECT && (errno == EAGAIN || errno == EINPROGRESS)) {
      io->watch_type = SHUSO_IO_WATCH_OP_FINISH;
      io_watch_update(io);
      return;
    }
    else if(errno == EAGAIN || errno == EWOULDBLOCK) {
      //-1 means zero bytes processed, in case of EAGAIN, right?
      io->watch_type = SHUSO_IO_WATCH_OP_RETRY;
      io_watch_update(io);
      return;
    }
    else {
      //legit error happened
      io->result = -1;
      io->error = errno;
      io_run(io);
      return;
    }
  }
  else if(io->op_repeat_to_completion && !io_op_update_and_check_completion(io, result)) {
    io->watch_type = SHUSO_IO_WATCH_OP_RETRY;
    io_watch_update(io);
    return;
  }
  io->result = result;
  io_run(io);
  return;
}

static void shuso_io_operation(shuso_io_t *io) {
#ifdef SHUTTLESOCK_DEBUG_IO
  switch((shuso_io_opcode_t )io->opcode) {
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
    default:
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

static void io_op_run_new(shuso_io_t *io, int opcode, void *init_ptr, ssize_t init_len, bool partial, bool registered) {
  io->result = 0;
  io->error = 0;
  io->opcode = opcode;
  switch(opcode) {
    case SHUSO_IO_OP_READV:
    case SHUSO_IO_OP_WRITEV:
      io->iov = init_ptr;
      io->iovcnt = init_len;
      break;
    case SHUSO_IO_OP_ACCEPT:
      io->sockaddr = init_ptr;
      io->len = init_len;
      assert(init_len == sizeof(*io->sockaddr));
      break;
    default:
      io->buf = init_ptr;
      io->len = init_len;
      break;
  }
  io->op_repeat_to_completion = !partial;
  io->op_registered_memory_buffer = registered;
  if(opcode == SHUSO_IO_OP_CLOSE) {
    raise(SIGSTOP);
  }
  shuso_io_operation(io);
}

void shuso_io_stop(shuso_io_t *io) {
  switch((shuso_io_watch_type_t )io->watch_type) {
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
      io->handler(io->S, io);
  }
  shuso_io_abort(io);
}

void shuso_io_abort(shuso_io_t *io) {
  io->watch_type = SHUSO_IO_WATCH_NONE;
  io->error = ECANCELED;
  io->result = -1;
  io_watch_update(io);
  //stop it at once!
}

void shuso_io_suspend(shuso_io_t *io, void *meh) {
  io->opcode = SHUSO_IO_OP_NONE;
  io->watch_type = SHUSO_IO_WATCH_NONE;
  io_watch_update(io);
  return;
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

void shuso_io_connect(shuso_io_t *io) {
  io_op_run_new(io, SHUSO_IO_OP_CONNECT, NULL, 0, false, false);
}
void shuso_io_shutdown(shuso_io_t *io, int rw) {
  int how = 0;
  if(rw == (SHUSO_IO_READ | SHUSO_IO_WRITE)) {
    how = SHUT_RDWR;
  }
  else if(rw == SHUSO_IO_READ) {
    how = SHUT_RD;
  }
  else if(rw == SHUSO_IO_WRITE) {
    how = SHUT_WR;
  }
  io_op_run_new(io, SHUSO_IO_OP_CLOSE, NULL, how, false, false);
}
void shuso_io_close(shuso_io_t *io) {
  io_op_run_new(io, SHUSO_IO_OP_CLOSE, NULL, 0, false, false);
}
void shuso_io_accept(shuso_io_t *io, shuso_sockaddr_t *sockaddr_buffer, socklen_t len) {
  assert(len == sizeof(*sockaddr_buffer));
  io_op_run_new(io, SHUSO_IO_OP_ACCEPT, sockaddr_buffer, len, false, false);
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

