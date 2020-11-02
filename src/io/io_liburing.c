#include <shuttlesock.h>
#include "io_private.h"
#ifdef SHUTTLESOCK_HAVE_IO_URING

#include <liburing.h>
#include <poll.h>

static void io_uring_cqe_op_handler(shuso_t *S, int32_t ret, uint32_t flags, shuso_io_uring_handle_t *handle, void *pd);
static void io_uring_cqe_timeout_handler(shuso_t *S, int32_t ret, uint32_t flags, shuso_io_uring_handle_t *handle, void *pd);
static void io_uring_cqe_cancel_handler(shuso_t *S, int32_t ret, uint32_t flags, shuso_io_uring_handle_t *handle, void *pd);

void shuso_io_uring_init_socket(shuso_t *S, shuso_io_t *io, shuso_socket_t *sock, int readwrite, shuso_io_fn *coro, void *privdata) {
  //TODO: register socket maybe? for now, nothing.
}

void shuso_io_uring_operation(shuso_io_t *io) {
  shuso_io_opcode_t       op = io->opcode;
  int                     fd = io->io_socket.fd;
  shuso_io_ioring_state_t *iors = &io->uring; //ioring state
  
  assert(!iors->active);
  
  struct io_uring_sqe *sqe = shuso_io_uring_get_sqe(io->S);
  if(sqe == NULL) {
    //legit error happened
    io->result = -1;
    io->error = errno;
    shuso_io_run_handler(io);
    return;
  }
  io_uring_sqe_set_data(sqe, &io->uring.handle);
  io->uring.handle = (shuso_io_uring_handle_t ){
    .callback = io_uring_cqe_op_handler,
    .pd = io
  };
  
  switch(op) {
    case SHUSO_IO_OP_NONE:
      //should never happen
      raise(SIGABRT);
      break;
    
    case SHUSO_IO_OP_READ:
      io_uring_prep_read(sqe, fd, io->buf, io->len, 0);
      break;
    case SHUSO_IO_OP_WRITE:
      io_uring_prep_write(sqe, fd, io->buf, io->len, 0);
      break;
    case SHUSO_IO_OP_READV:
      io_uring_prep_readv(sqe, fd, io->iov, io->iovcnt, 0);
      break;
    case SHUSO_IO_OP_WRITEV:
      io_uring_prep_writev(sqe, fd, io->iov, io->iovcnt, 0);
      break;
    case SHUSO_IO_OP_SENDMSG:
      io_uring_prep_sendmsg(sqe, fd, io->msg, io->flags);
      break;
    case SHUSO_IO_OP_RECVMSG:
      io_uring_prep_recvmsg(sqe, fd, io->msg, io->flags);
      break;
    case SHUSO_IO_OP_RECVFROM:
      //TODO: implement via recvmsg
      break;
    case SHUSO_IO_OP_RECV:
      io_uring_prep_recv(sqe, fd, io->buf, io->len, io->flags);
      break;
    case SHUSO_IO_OP_SENDTO: 
      //TODO: implement via sendmsg
      break;
    case SHUSO_IO_OP_SEND:
      io_uring_prep_send(sqe, fd, io->buf, io->len, io->flags);
      break;
    case SHUSO_IO_OP_CONNECT: {
      if(fd == -1) {
        io->result = -1;
        errno = EBADF;
        io->error = errno;
        shuso_io_run_handler(io);
        return;
      }
      assert(io->io_socket.host.sockaddr);
      iors->addrlen = shuso_io_af_sockaddrlen(io->io_socket.host.sockaddr->any.sa_family);
      io_uring_prep_connect(sqe, fd, &io->io_socket.host.sockaddr->any, iors->addrlen);
      break;
    }
    case SHUSO_IO_OP_ACCEPT: {
      iors->addrlen = io->len;
      assert(iors->addrlen == sizeof(*io->sockaddr));
      assert(io->sockaddr != NULL);
      io_uring_prep_accept(sqe, fd, &io->sockaddr->any, &iors->addrlen, SOCK_NONBLOCK);
      break;
    }
    case SHUSO_IO_OP_CLOSE:
      io_uring_prep_close(sqe, fd);
      break;
    case SHUSO_IO_OP_SHUTDOWN:
      //TODO: uuh... how do I implement this?...
      break;
  }
  iors->sqe_opcode = sqe->opcode;
  iors->sqe_flags = sqe->flags;
  iors->active = true;
  
  shuso_io_uring_submit(io->S);
}

static short io_uring_poll_mask(shuso_io_t *io) {
  switch(io->watch_type) {
    case SHUSO_IO_WATCH_NONE:
      return 0;
      
    case SHUSO_IO_WATCH_POLL_READ:
      return POLLIN;
      
    case SHUSO_IO_WATCH_POLL_WRITE:
      return POLLOUT;
      
    case SHUSO_IO_WATCH_POLL_READWRITE:
      return POLLIN | POLLOUT;
      
    case SHUSO_IO_WATCH_OP_FINISH:
    case SHUSO_IO_WATCH_OP_RETRY:
      switch(io->opcode) {
        case SHUSO_IO_OP_NONE:
          //should not happen
          raise(SIGABRT);
          return 0;
        
        case SHUSO_IO_OP_READV:
        case SHUSO_IO_OP_READ:
        case SHUSO_IO_OP_RECVFROM:
        case SHUSO_IO_OP_RECVMSG:
        case SHUSO_IO_OP_RECV:
          return POLLIN;
        
        case SHUSO_IO_OP_WRITEV:
        case SHUSO_IO_OP_WRITE:
        case SHUSO_IO_OP_SENDTO:
        case SHUSO_IO_OP_SENDMSG:
        case SHUSO_IO_OP_SEND:
          return POLLOUT;
          
        case SHUSO_IO_OP_ACCEPT:
        case SHUSO_IO_OP_CONNECT:
        case SHUSO_IO_OP_CLOSE:
        case SHUSO_IO_OP_SHUTDOWN:
          //not supposed to happen, as io_uring performs these to completion... right?...
          raise(SIGABRT);
          return 0;
      }
      break;
  }
  return 0;
}

static bool shuso_io_uring_watcher_stop(shuso_io_t *io) {
  assert(io->uring.active);
  assert(io->uring.watching);
  if(!io->uring.cancel_active) {
    return true;
  }
  struct io_uring_sqe *sqe = shuso_io_uring_get_sqe(io->S);
  if(sqe == NULL) {
    shuso_log_error(io->S, "Can't cancel io_uring watcher for io %p: failed to get an SQE", io);
    return false;
  }
  
  io->uring.cancel_handle = (shuso_io_uring_handle_t ) {
    .callback = io_uring_cqe_cancel_handler,
    .pd = io
  };
  io_uring_prep_poll_remove(sqe, &io->uring.handle);
  io->uring.cancel_active = true;
  return true;
}

static bool shuso_io_uring_watcher_start(shuso_io_t *io) {
  assert(!io->uring.active);
  assert(!io->uring.watching);
  
  struct io_uring_sqe *sqe = shuso_io_uring_get_sqe(io->S);
  if(sqe == NULL) {
    shuso_log_error(io->S, "Can't start io_uring watcher for io %p: failed to get an SQE", io);
    return false;
  }
  
  short pollmask = io_uring_poll_mask(io);
  assert(pollmask != 0);
  io_uring_prep_poll_add(sqe, io->io_socket.fd, pollmask);
  io->uring.watching = true;
  io->uring.active = true;
  shuso_io_uring_submit(io->S);
  return true;
}

void shuso_io_uring_watch_update(shuso_io_t *io) {
  bool watching = io->uring.watching;
  if(io->watch_type == SHUSO_IO_WATCH_NONE && watching) {
    //watcher needs to be stopped
    shuso_io_uring_watcher_stop(io);
  }
  else if(io->watch_type != SHUSO_IO_WATCH_NONE && !watching) {
    //watcher needs to be started
    shuso_io_uring_watcher_start(io);
  }
}

static const char *shuso_io_uring_strerror(int error, uint8_t sqe_opcode, uint8_t sqe_flags) {
  switch(error) {
#ifdef EACCESS
    case EACCESS:
      return "The flags field or opcode in a submission queue entry is not allowed  due  to  registered restrictions";
#endif
    case EBADF:
      return "The fd field in the submission queue entry is invalid, or the IOSQE_FIXED_FILE flag was set in the submission queue entry, but no files were registered with the io_uring instance.";
    case EFAULT:
      if(sqe_opcode == IORING_OP_READ_FIXED || sqe_opcode == IORING_OP_WRITE_FIXED) {
        return "IORING_OP_READ_FIXED or IORING_OP_WRITE_FIXED was specified in the opcode field of the submission queue entry, but either buffers were not registered for this io_uring instance, or the address range described by addr and len does not fit within the buffer registered at buf_index.";
      }
      else {
        return "Buffer is outside of the process' accessible address space";
      }
    case EINVAL:
      if(sqe_opcode == IORING_OP_READV || sqe_opcode == IORING_OP_WRITEV) {
       return "IORING_OP_READV or IORING_OP_WRITEV was specified in the submission queue entry,  but the io_uring instance has fixed buffers registered.";
      }
      else if(sqe_opcode == IORING_OP_POLL_ADD) {
        return "IORING_OP_POLL_ADD was specified in the opcode field of the submission queue entry, and the addr field was non-zero.";
      }
      else if(sqe_opcode == IORING_OP_READ_FIXED || sqe_opcode == IORING_OP_WRITE_FIXED) {
        return "IORING_OP_READ_FIXED  or IORING_OP_WRITE_FIXED was specified in the submission queue entry, and the buf_index is invalid.";
      }
      else {
        return "The flags field or opcode or buf_index member or personality field in a submission queue entry is invalid.";
      }
      //we don't handle the IORING_SETUP_IOPOLL-related errors, because we don't expect to use IORING_SETUP_IOPOLL. (Rather, we may use IORING_SETUP_SQPOLL instead which doesn't genrate these errors)
    case EOPNOTSUPP:
#ifdef IOSQE_BUFFER_SELECT
      if(sqe_flags & IOSQE_BUFFER_SELECT) {
        return "IOSQE_BUFFER_SELECT was set in the flags field of the submission queue entry, but the opcode doesn't support buffer selection.";
      }
#endif
      return "opcode is valid, but not supported by this kernel";
    default:
      //unexpected error type
      assert(0);
  }
}


static void io_uring_cqe_op_handler(shuso_t *S, int32_t ret, uint32_t flags, shuso_io_uring_handle_t *handle, void *pd) {
  shuso_io_t             *io = pd;
  shuso_io_opcode_t       op = io->opcode;
  assert(io->uring.active);
  io->uring.active = false;
  assert(S == io->S);
  shuso_io_update_fd_closed_status_from_op_result(io, op, ret); //handles ret == 0
  if(ret < 0) { //an error
    io->result = -1;
    io->error = -ret;
    io->strerror = shuso_io_uring_strerror(io->error, io->uring.sqe_opcode, io->uring.sqe_flags);
    return;
  }
  else if(ret != 0 && io->op_repeat_to_completion && !shuso_io_op_update_and_check_completion(io, ret)) {
    io->watch_type = SHUSO_IO_WATCH_OP_RETRY;
    io->result += ret;
    shuso_io_uring_watch_update(io);
    return;
  }
  shuso_io_op_cleanup(io);
  io->result += ret;
  shuso_io_run_handler(io);
  return;
}

static void io_uring_cqe_cancel_handler(shuso_t *S, int32_t ret, uint32_t flags, shuso_io_uring_handle_t *handle, void *pd) {
  shuso_io_t *io = pd;
  assert(io->uring.active);
  assert(io->uring.cancel_active);
  
  //TODO: handle cancelation
}

static void io_uring_cqe_timeout_handler(shuso_t *S, int32_t ret, uint32_t flags, shuso_io_uring_handle_t *handle, void *pd) {
  //TODO: timeout handler
}

#endif
