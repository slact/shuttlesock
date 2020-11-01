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

#include "io_private.h"
#include "io_libev.h"

static bool ev_opcode_finish_match_event_type(shuso_io_opcode_t opcode, int evflags) {
  switch(opcode) {
    case SHUSO_IO_OP_NONE:
    case SHUSO_IO_OP_READV:
    case SHUSO_IO_OP_READ:
    case SHUSO_IO_OP_RECVFROM:
    case SHUSO_IO_OP_RECVMSG:
    case SHUSO_IO_OP_RECV:
    case SHUSO_IO_OP_WRITEV:
    case SHUSO_IO_OP_WRITE:
    case SHUSO_IO_OP_SENDTO:
    case SHUSO_IO_OP_SENDMSG:
    case SHUSO_IO_OP_SEND:
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
    case SHUSO_IO_OP_RECVFROM:
    case SHUSO_IO_OP_RECVMSG:
    case SHUSO_IO_OP_RECV:
      return evflags & EV_READ;
    
    case SHUSO_IO_OP_WRITEV:
    case SHUSO_IO_OP_WRITE:
    case SHUSO_IO_OP_SENDTO:
    case SHUSO_IO_OP_SENDMSG:
    case SHUSO_IO_OP_SEND:
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

void shuso_io_ev_watcher_handler(shuso_loop *loop, shuso_ev_io *ev, int evflags) {
  shuso_io_t *io = shuso_ev_data(ev);
  switch((shuso_io_watch_type_t )io->watch_type) {    
    case SHUSO_IO_WATCH_NONE:
      //should never happer
      raise(SIGABRT);
      break;
    
    case SHUSO_IO_WATCH_OP_FINISH:
      if(ev_opcode_finish_match_event_type(io->opcode, evflags)) {
        shuso_io_ev_operation(io);
      }
      break;
      
    case SHUSO_IO_WATCH_OP_RETRY:
      if(ev_opcode_retry_match_event_type(io->opcode, evflags)) {
        io->watch_type = SHUSO_IO_WATCH_NONE;
        assert(io->opcode != SHUSO_IO_OP_NONE);
        shuso_io_ev_operation(io);
      }
      break;
    
    case SHUSO_IO_WATCH_POLL_READ:
    case SHUSO_IO_WATCH_POLL_WRITE:
    case SHUSO_IO_WATCH_POLL_READWRITE:
      if(ev_watch_poll_match_event_type(io->watch_type, evflags)) {
        io->watch_type = SHUSO_IO_WATCH_NONE;
        shuso_io_run_handler(io);
      }
      break;
  }
}

void shuso_io_ev_watch_update(shuso_io_t *io) {
  assert(!io->use_io_uring);
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

void shuso_io_ev_init_socket(shuso_t *S, shuso_io_t *io, shuso_socket_t *sock, int readwrite, shuso_io_fn *coro, void *privdata) {
  int events = 0;
  if(io->readwrite & SHUSO_IO_READ) {
    events |= SHUSO_IO_READ;
  }
  if(io->readwrite & SHUSO_IO_WRITE) {
    events |= SHUSO_IO_WRITE;
  }
  shuso_ev_init(io->S, &io->watcher, io->io_socket.fd, events, shuso_io_ev_watcher_handler, io);
}

int shuso_io_ev_connect(shuso_io_t *io) {
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
    shuso_io_ev_watch_update(io);
    return rc;
  }
  
  shuso_sockaddr_t     *connect_sockaddr;
  size_t                sockaddr_sz;
  
  assert(io->io_socket.host.sockaddr);
  
  connect_sockaddr = io->io_socket.host.sockaddr;
  sockaddr_sz = shuso_io_af_sockaddrlen(io->io_socket.host.sockaddr->any.sa_family);
  
  return connect(io->io_socket.fd, &connect_sockaddr->any, sockaddr_sz);
}

void shuso_io_ev_operation(shuso_io_t *io) {
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
  ssize_t result;
  shuso_io_opcode_t   op = io->opcode;
  int                 fd = io->io_socket.fd;
  
  do {
    switch(op) {
      case SHUSO_IO_OP_NONE:
        //should never happen
        raise(SIGABRT);
        break;
      
      case SHUSO_IO_OP_READ:
        result = read(fd, io->buf, io->len);
        break;
      case SHUSO_IO_OP_WRITE:
        assert(io->len > 0);
        result = write(fd, io->buf, io->len);
        break;
      case SHUSO_IO_OP_READV:
        result = readv(fd, io->iov, io->iovcnt);
        break;
      case SHUSO_IO_OP_WRITEV:
        result = writev(fd, io->iov, io->iovcnt);
        break;
      case SHUSO_IO_OP_SENDMSG:
        result = sendmsg(fd, io->msg, io->flags);
        break;
      case SHUSO_IO_OP_RECVMSG:
        result = recvmsg(fd, io->msg, io->flags);
        break;
      case SHUSO_IO_OP_RECVFROM: {
        assert(io->sockaddr);
        socklen_t sockaddrlen = shuso_io_af_sockaddrlen(io->sockaddr->any.sa_family);
        result = recvfrom(fd, io->buf, io->len, io->flags, &io->sockaddr->any, &sockaddrlen);
        break;
      }
      case SHUSO_IO_OP_RECV:
        result = recv(fd, io->buf, io->len, io->flags);
        break;
      case SHUSO_IO_OP_SENDTO: 
        assert(io->sockaddr);
        result = sendto(fd, io->buf, io->len, io->flags, &io->sockaddr->any, shuso_io_af_sockaddrlen(io->sockaddr->any.sa_family));
        break;
      case SHUSO_IO_OP_SEND:
        result = send(fd, io->buf, io->len, io->flags);
        break;
      case SHUSO_IO_OP_CONNECT:
        result = shuso_io_ev_connect(io);
        break;
      case SHUSO_IO_OP_ACCEPT: {
        socklen_t         socklen = io->len;
        assert(socklen == sizeof(*io->sockaddr));
        assert(io->sockaddr != NULL);
#ifdef SHUTTLESOCK_HAVE_ACCEPT4
        result = accept4(fd, &io->sockaddr->any, &socklen, SOCK_NONBLOCK);
#else
        result = accept(fd, &io->sockaddr->any, &socklen);
        if(result != -1) {
          fcntl(result, F_SETFL, O_NONBLOCK);
        }
#endif
        io->len = socklen;
        break;
      }
      case SHUSO_IO_OP_CLOSE:
        result = close(fd);
        break;
      case SHUSO_IO_OP_SHUTDOWN:
        result = shutdown(fd, io->flags);
        break;
    }
  } while(result == -1 && errno == EINTR);
  
  shuso_io_update_fd_closed_status_from_op_result(io, op, result);
  
  if(result == -1) {
    if(op == SHUSO_IO_OP_CONNECT && (errno == EAGAIN || errno == EINPROGRESS)) {
      io->watch_type = SHUSO_IO_WATCH_OP_FINISH;
      shuso_io_ev_watch_update(io);
      return;
    }
    else if(errno == EAGAIN || errno == EWOULDBLOCK) {
      //-1 means zero bytes processed, in case of EAGAIN, right?
      io->watch_type = SHUSO_IO_WATCH_OP_RETRY;
      shuso_io_ev_watch_update(io);
      return;
    }
    else {
      //legit error happened
      io->result = -1;
      io->error = errno;
      shuso_io_op_cleanup(io);
      shuso_io_run_handler(io);
      return;
    }
  }
  else if(result != 0 && io->op_repeat_to_completion && !shuso_io_op_update_and_check_completion(io, result)) {
    io->watch_type = SHUSO_IO_WATCH_OP_RETRY;
    io->result += result;
    shuso_io_ev_watch_update(io);
    return;
  }
  shuso_io_op_cleanup(io);
  io->result += result;
  shuso_io_run_handler(io);
  return;
}


