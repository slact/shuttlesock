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

#include "io_private.h"
#include "io_libev.h"
#include "io_liburing.h"

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
    .closed = 0,
    .readwrite = readwrite
  };
  if(sock) {
    io->io_socket = *sock;
  }
  
  if(io->use_io_uring) {
    shuso_io_uring_init_socket(S, io, sock, readwrite, coro, privdata);
  }
  else {
    shuso_io_ev_init_socket(S, io, sock, readwrite, coro, privdata);
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

static void shuso_io_watch_update(shuso_io_t *io) {
  if(io->use_io_uring) {
    shuso_io_uring_watch_update(io);
  }
  else {
    shuso_io_ev_watch_update(io);
  }
}

void shuso_io_run_handler(shuso_io_t *io) {
  if(io->error_handler && io->result == -1) {
    io->handler_stage = 0;
    io->error_handler(io->S, io);
  }
  else {
    io->handler(io->S, io);
    shuso_io_watch_update(io);
  }
}

void shuso_io_resume_buf(shuso_io_t *io, char *buf, size_t len) {
  io->buf = buf;
  io->len = len;
  shuso_io_run_handler(io);
}
void shuso_io_resume_iovec(shuso_io_t *io, struct iovec *iov, int iovcnt) {
  io->iov = iov;
  io->iovcnt = iovcnt;
  shuso_io_run_handler(io);
}
void shuso_io_resume_msg(shuso_io_t *io, struct msghdr *msg, int flags) {
  io->msg = msg;
  io->flags = flags;
  shuso_io_run_handler(io);
}

void shuso_io_resume_data(shuso_io_t *io, void *data, int intdata) {
  io->result_data = data;
  io->intdata = intdata;
  shuso_io_run_handler(io);
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

static bool io_iovec_update_on_incomplete_op(shuso_io_t *io, struct iovec **iovptr, size_t *iovcnt_ptr, size_t iovcnt_offset, size_t iov_start_offset) {
  if(iov_start_offset == 0) {
    if(!io->incomplete_original_iovec) {
      io->incomplete_original_iovec = *iovptr;
    }
    assert(*iovcnt_ptr > iovcnt_offset);
    *iovptr = &(*iovptr)[iovcnt_offset];
    *iovcnt_ptr -= iovcnt_offset;
  }
  else {
    if(!io->incomplete_temporary_iovec) {
      ssize_t iovcnt = *iovcnt_ptr;
      ssize_t new_iovcnt = iovcnt - iovcnt_offset;
      assert(iovcnt > 0);
      assert(new_iovcnt > 0);
      assert(iovcnt + iovcnt_offset > 0);
      struct iovec *iov = *iovptr;
      struct iovec *tmp_iov = malloc(sizeof(*tmp_iov) * (size_t)new_iovcnt);
      if(tmp_iov == NULL) {
        io->error = errno;
        io->result = -1;
        return false;
      }

      for(unsigned i = 0; i < iovcnt + iovcnt_offset; i++) {
        tmp_iov[i] = iov[i+iovcnt_offset];
      }
      assert(tmp_iov[0].iov_len > iov_start_offset);
      tmp_iov[0].iov_len -= iov_start_offset;
      tmp_iov[0].iov_base = &((char *)tmp_iov[0].iov_base)[iov_start_offset];
      
      io->incomplete_temporary_iovec = tmp_iov;
      if(!io->incomplete_original_iovec) {
        io->incomplete_original_iovec = *iovptr;
      }
      assert(*iovcnt_ptr > iovcnt_offset);
      *iovcnt_ptr -= iovcnt_offset;
      *iovptr = tmp_iov;
    }
    else {
      assert(io->incomplete_original_iovec != NULL);
      struct iovec *iov = *iovptr;
      assert(iov[iovcnt_offset].iov_len > iov_start_offset);
      assert(*iovcnt_ptr > iovcnt_offset);
      *iovptr = &iov[iovcnt_offset];
      *iovcnt_ptr -= iovcnt_offset;
    }
  }
  return true;
}

static bool io_iovec_op_update_and_check_completion(shuso_io_t *io, struct iovec **iov_ptr, size_t *iovcnt_ptr, ssize_t written) {
  //returns true if operation is complete (no more iovec to write, or there's an error)
  size_t         written_unaccounted_for = written;
  size_t         iovcnt = *iovcnt_ptr;
  size_t         i;
  struct iovec  *iov = *iov_ptr;
  for(i = 0; i < iovcnt && written_unaccounted_for > 0; i++) {
    size_t len = iov[i].iov_len;
    if(written_unaccounted_for < len) {
      //ended in the middle of an iovec
      if(!io_iovec_update_on_incomplete_op(io, iov_ptr, iovcnt_ptr, i, written_unaccounted_for)) {
        assert(io->error != 0);
        return true; //finished with error
      }
      return false;
    }
    else {
      written_unaccounted_for -= len;
    }
  }
  if(written_unaccounted_for > 0) {
    //ended between iovecs
    if(!io_iovec_update_on_incomplete_op(io, iov_ptr, iovcnt_ptr, i, 0)) {
      assert(io->error != 0);
      return true; //finished with error
    }
    return false;
  }
  return true;
}

bool shuso_io_op_update_and_check_completion(shuso_io_t *io, ssize_t result_sz) {
  size_t  iovcnt;
  bool    ret;
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
    case SHUSO_IO_OP_SEND:
    case SHUSO_IO_OP_RECV:
      io->buf = &io->buf[result_sz];
      io->len -= result_sz;
      assert(io->len >= 0);
      return io->len == 0;
      
    case SHUSO_IO_OP_READV:
    case SHUSO_IO_OP_WRITEV:
      iovcnt = io->iovcnt;
      ret = io_iovec_op_update_and_check_completion(io, &io->iov, &iovcnt, result_sz);
      io->iovcnt = iovcnt;
      return ret;
      
    case SHUSO_IO_OP_RECVMSG:
    case SHUSO_IO_OP_SENDMSG:
      iovcnt = (size_t )io->msg->msg_iovlen;
      ret = io_iovec_op_update_and_check_completion(io, &io->msg->msg_iov, &iovcnt, result_sz);
      io->msg->msg_iovlen = iovcnt;
      return ret;
  }
  return true;
}



socklen_t shuso_io_af_sockaddrlen(sa_family_t fam) {
  switch(fam) {
    case AF_INET:
      return sizeof(struct sockaddr_in);
#ifdef SHUTTLESOCK_HAVE_IPV6
    case AF_INET6:
      return sizeof(struct sockaddr_in6);
#endif
    case AF_UNIX:
      return sizeof(struct sockaddr_un);
  }
  //unknown
  return 0;
}

void shuso_io_update_fd_closed_status_from_op_result(shuso_io_t *io, shuso_io_opcode_t op, int result) {
//if we got a 0 from a read or write operation, mark this thing closed
  switch(op) {
    case SHUSO_IO_OP_READ:
    case SHUSO_IO_OP_READV:
    case SHUSO_IO_OP_RECVFROM:
    case SHUSO_IO_OP_RECV:
    case SHUSO_IO_OP_RECVMSG:
      if(result == 0) {
        io->closed |= SHUSO_IO_READ;
      }
      else {
        io->closed &= ~SHUSO_IO_READ;
      }
      break;
    case SHUSO_IO_OP_WRITE:
    case SHUSO_IO_OP_WRITEV:
    case SHUSO_IO_OP_SENDMSG:
    case SHUSO_IO_OP_SENDTO:
    case SHUSO_IO_OP_SEND:
      if(result == 0) {
        io->closed |= SHUSO_IO_WRITE;
      }
      else {
        io->closed &= ~SHUSO_IO_WRITE;
      }
      break;
      
    default:
      break;
  }
}

void shuso_io_op_cleanup(shuso_io_t *io) {
  switch((shuso_io_opcode_t )io->opcode) {
    case SHUSO_IO_OP_READV:
    case SHUSO_IO_OP_WRITEV:
    case SHUSO_IO_OP_SENDMSG:
    case SHUSO_IO_OP_RECVMSG:
      if(io->incomplete_temporary_iovec) {
        free(io->incomplete_temporary_iovec);
        io->incomplete_temporary_iovec = NULL;
      }
      break;
    default:
      break;
  }
}

static void io_op_run_new(shuso_io_t *io, shuso_io_opcode_t opcode, void *init_ptr, ssize_t init_len, bool partial, bool registered) {
  io->result = 0;
  io->error = 0;
  io->op_again = 0;
  io->opcode = opcode;
  switch(opcode) {
    case SHUSO_IO_OP_READV:
    case SHUSO_IO_OP_WRITEV:
      io->iov = init_ptr;
      io->iovcnt = init_len;
      io->incomplete_original_iovec = NULL;
      io->incomplete_temporary_iovec = NULL;
      break;
    case SHUSO_IO_OP_SENDMSG:
    case SHUSO_IO_OP_RECVMSG:
      io->msg = init_ptr;
      io->len = init_len;
      io->incomplete_original_iovec = NULL;
      io->incomplete_temporary_iovec = NULL;
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
  if(io->use_io_uring) {
    shuso_io_uring_operation(io);
  }
  else {
    shuso_io_ev_operation(io);
  }
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
  if(io->use_io_uring) {
    //TODO
    abort();
  }
  else {
    shuso_io_op_cleanup(io);
  }
  io->watch_type = SHUSO_IO_WATCH_NONE;
  io->error = ECANCELED;
  io->result = -1;
  shuso_io_watch_update(io);
  //stop it at once!
}

void shuso_io_suspend(shuso_io_t *io, void *meh) {
  io->opcode = SHUSO_IO_OP_NONE;
  io->watch_type = SHUSO_IO_WATCH_NONE;
  shuso_io_watch_update(io);
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
  shuso_io_watch_update(io);
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

void shuso_io_sendto_partial(shuso_io_t *io, const void *buf, size_t len, shuso_sockaddr_t *to, int flags) {
  io->flags = flags;
  io->sockaddr = to;
  io_op_run_new(io, SHUSO_IO_OP_SENDTO, (void *)buf, len, true, false);
}
void shuso_io_sendto(shuso_io_t *io, const void *buf, size_t len, shuso_sockaddr_t *to, int flags) {
  io->flags = flags;
  io->sockaddr = to;
  io_op_run_new(io, SHUSO_IO_OP_SENDTO, (void *)buf, len, false, false);
}
void shuso_io_recvfrom_partial(shuso_io_t *io, void *buf, size_t len, shuso_sockaddr_t *from, int flags) {
  io->flags = flags;
  io->sockaddr = from;
  io_op_run_new(io, SHUSO_IO_OP_RECVFROM, buf, len, true, false);
}
void shuso_io_recvfrom(shuso_io_t *io, void *buf, size_t len, shuso_sockaddr_t *from, int flags) {
  io->flags = flags;
  io->sockaddr = from;
  io_op_run_new(io, SHUSO_IO_OP_RECVFROM, buf, len, false, false);
}

void shuso_io_send_partial(shuso_io_t *io, const void *buf, size_t len, int flags) {
  io->flags = flags;
  io_op_run_new(io, SHUSO_IO_OP_SEND, (void *)buf, len, true, false);
}
void shuso_io_send(shuso_io_t *io, const void *buf, size_t len, int flags) {
  io->flags = flags;
  io_op_run_new(io, SHUSO_IO_OP_SEND, (void *)buf, len, false, false);
}
void shuso_io_recv_partial(shuso_io_t *io, void *buf, size_t len, int flags) {
  io->flags = flags;
  io_op_run_new(io, SHUSO_IO_OP_RECV, buf, len, true, false);
}
void shuso_io_recv(shuso_io_t *io, void *buf, size_t len, int flags) {
  io->flags = flags;
  io_op_run_new(io, SHUSO_IO_OP_RECV, buf, len, false, false);
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
  io->flags = flags;
  io_op_run_new(io, SHUSO_IO_OP_SENDMSG, msg, 0, false, false);
}
void shuso_io_recvmsg(shuso_io_t *io, struct msghdr *msg, int flags) {
  io->flags = flags;
  io_op_run_new(io, SHUSO_IO_OP_RECVMSG, msg, 0, false, false);
}

