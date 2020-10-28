#include <shuttlesock.h>

#include <liburing.h>


static void shuso_io_uring_get_sqe(io) {
  shuso_t *S = io->S;
  struct io_uring_sqe *sqe = io_uring_get_sqe(&S->io_uring.ring);
  if(!sqe) {
    //submission queue is full. what do?
    //TODO: handle this situation by forcing the sqeueue to be read or waiting around
    return;
  }
  
  io->uring_coro.sqe = sqe;
  sqe->user_data = &io->uring_coro.handle;
  assert(io->uring_coro.handle.pd == io);
  assert(io->uring_coro.handle.handler == io);
  //'resume' coro right away
  shuso_io_uring_operation(io);
}

void shuso_io_uring_operation(shuso_io_t *io) {
  shuso_io_opcode_t     op = io->opcode;
  int                   fd = io->io_socket.fd;
  
  
  IORING_CORO_BEGIN(&io->io_uring.coro_stage);
  
  IORING_CORO_YIELD(shuso_io_uring_get_sqe(io));
  sqe = io->io_uring.sqe;
  assert(sqe);
  
  switch(op) {
    case SHUSO_IO_OP_NONE:
      //should never happen
      raise(SIGABRT)
      break;
    
    case SHUSO_IO_OP_READ:
      break;
    case SHUSO_IO_OP_WRITE:
      //assert(io->len > 0);
      //result = write(fd, io->buf, io->len);
      break;
    case SHUSO_IO_OP_READV:
      //result = readv(fd, io->iov, io->iovcnt);
      break;
    case SHUSO_IO_OP_WRITEV:
      //result = writev(fd, io->iov, io->iovcnt);
      break;
    case SHUSO_IO_OP_SENDMSG:
      //result = sendmsg(fd, io->msg, io->flags);
      break;
    case SHUSO_IO_OP_RECVMSG:
      //result = recvmsg(fd, io->msg, io->flags);
      break;
    case SHUSO_IO_OP_RECVFROM: {
      //assert(io->sockaddr);
      //socklen_t sockaddrlen = shuso_io_af_sockaddrlen(io->sockaddr->any.sa_family);
      //result = recvfrom(fd, io->buf, io->len, io->flags, &io->sockaddr->any, &sockaddrlen);
      break;
    }
    case SHUSO_IO_OP_RECV:
      //result = recv(fd, io->buf, io->len, io->flags);
      break;
    case SHUSO_IO_OP_SENDTO: 
      //assert(io->sockaddr);
      //result = sendto(fd, io->buf, io->len, io->flags, &io->sockaddr->any, shuso_io_af_sockaddrlen(io->sockaddr->any.sa_family));
      break;
    case SHUSO_IO_OP_SEND:
      //result = send(fd, io->buf, io->len, io->flags);
      break;
    case SHUSO_IO_OP_CONNECT:
      //result = shuso_io_ev_connect(io);
      break;
    case SHUSO_IO_OP_ACCEPT: {
      //socklen_t         socklen = io->len;
      //assert(socklen == sizeof(*io->sockaddr));
      //assert(io->sockaddr != NULL);
      //result = accept(fd, &io->sockaddr->any, &socklen);
      //if(result != -1) {
      //  fcntl(result, F_SETFL, O_NONBLOCK);
      //}
      //io->len = socklen;
      break;
    }
    case SHUSO_IO_OP_CLOSE:
      //result = close(fd);
      break;
    case SHUSO_IO_OP_SHUTDOWN:
      //result = shutdown(fd, io->flags);
      break;
  }
  
}
