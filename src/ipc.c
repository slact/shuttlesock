#ifndef _XPG4_2 /* Solaris needs this for sendmsg/recvmsg apparently? */
#define _XPG4_2
#endif
#if defined(__FreeBSD__)
#include <sys/param.h> /* FreeBSD needs this for sendmsg/recvmsg apparently? */
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <shuttlesock.h>
#include <shuttlesock/ipc.h>
#include <shuttlesock/log.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <shuttlesock/shared_slab.h>
#include <errno.h>
#ifdef SHUTTLESOCK_HAVE_EVENTFD
#include <sys/eventfd.h>
#endif

static void ipc_send_retry_cb(shuso_loop *loop, shuso_ev_timer *w, int revents);

#ifdef SHUTTLESOCK_DEBUG_IPC_RECEIVE_CHECK_TIMER
static void ipc_receive_check_cb(shuso_loop *loop, shuso_ev_timer *w, int revents);
#endif
static bool ipc_receive(shuso_t *S, shuso_process_t *proc);

bool shuso_ipc_channel_shared_create(shuso_t *S, shuso_process_t *proc) {
  int               procnum = shuso_process_to_procnum(S, proc);
  void             *ptr;
  
  assert(proc->ipc.buf == NULL);
  
  if(procnum == SHUTTLESOCK_MASTER || procnum == SHUTTLESOCK_MANAGER) {
    ptr = mmap(NULL, sizeof(shuso_ipc_ringbuf_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED,-1, 0);
  }
  else {
    ptr = calloc(1, sizeof(shuso_ipc_ringbuf_t));
  }
  if(!ptr) return false;
  proc->ipc.buf = ptr;
  proc->ipc.buf->full = false;
  
  int fds[2]={-1, -1};
#ifdef SHUTTLESOCK_HAVE_EVENTFD
  //straight out of libev
  fds[1] = eventfd(0, 0);
  if(fds[1] == -1) {
    return shuso_set_error(S, "failed to create IPC channel eventfd");
  }
  fcntl(fds[1], F_SETFL, O_NONBLOCK);
#else
  if(pipe(fds) == -1) {
    return shuso_set_error(S, "failed to create IPC channel pipe");
  }
  fcntl(fds[0], F_SETFL, O_NONBLOCK);
  fcntl(fds[1], F_SETFL, O_NONBLOCK);
#endif
  
  proc->ipc.fd[0] = fds[0];
  proc->ipc.fd[1] = fds[1];
  
  //open socket-transfer sockets
  if((socketpair(PF_LOCAL, SOCK_STREAM, 0, proc->ipc.socket_transfer_fd)) == -1) {
    return shuso_set_error(S, "failed to create IPC channel socket transfer socket");
  }
  fcntl(proc->ipc.socket_transfer_fd[0], F_SETFL, O_NONBLOCK);
  fcntl(proc->ipc.socket_transfer_fd[1], F_SETFL, O_NONBLOCK);
  return true;
}

bool shuso_ipc_channel_shared_destroy(shuso_t *S, shuso_process_t *proc) {
  int               procnum = shuso_process_to_procnum(S, proc);
  if(procnum == SHUTTLESOCK_MASTER || procnum == SHUTTLESOCK_MANAGER) {
    munmap(proc->ipc.buf, sizeof(shuso_ipc_ringbuf_t));
  }
  else {
    free(proc->ipc.buf);
  }
  proc->ipc.buf = NULL;
  if(proc->ipc.fd[0] != -1) {
    close(proc->ipc.fd[0]);
    proc->ipc.fd[0] = -1;
  }
  if(proc->ipc.fd[1] != -1) {
    close(proc->ipc.fd[1]);
    proc->ipc.fd[1] = -1;
  }
  if(proc->ipc.socket_transfer_fd[0] != -1) {
    close(proc->ipc.socket_transfer_fd[0]);
    proc->ipc.socket_transfer_fd[0] = -1;
  }
  if(proc->ipc.socket_transfer_fd[1] != -1) {
    close(proc->ipc.socket_transfer_fd[1]);
    proc->ipc.socket_transfer_fd[1] = -1;
  }
  return true;
}

void ipc_send_ipc_notice_coroutine(shuso_t *S, shuso_io_t *io) {
#ifdef SHUTTLESOCK_HAVE_EVENTFD
  static const uint64_t data = 1;
#else
  static const char data = 0;
#endif
  SHUSO_IO_CORO_BEGIN(io);
  SHUSO_IO_CORO_YIELD(write, &data, sizeof(data));
  assert(io->result == sizeof(data));
  SHUSO_IO_CORO_END(io);
}

void ipc_send_fd_coroutine(shuso_t *S, shuso_io_t *io) {
  shuso_buffer_t        *buf = io->privdata;
  shuso_buffer_link_t   *next;
  SHUSO_IO_CORO_BEGIN(io);
  while(!io->error && (next = shuso_buffer_next(S, buf)) != NULL) {
    SHUSO_IO_CORO_YIELD(sendmsg, next->msg, 0);
    
    shuso_buffer_free(S, buf, shuso_buffer_dequeue(S, buf)); // cleans up before free()ing, which runs the completion callback
  };
  SHUSO_IO_CORO_END(io);
  
  //TODO: cleanup buffer in case an error interrupted buffered  
}
static void ipc_channel_local_send_coroutines_init(shuso_t *S, int procnum) {
  shuso_process_t *dstproc = shuso_process(S, procnum);
  shuso_io_send_t *send = &S->ipc.io.send[procnum];
  
  assert(dstproc);
  
  shuso_io_coro_init(S, &send->notice, dstproc->ipc.fd[1], SHUSO_IO_WRITE, ipc_send_ipc_notice_coroutine, NULL);
  
  shuso_buffer_init(S, &send->fd_msg_buf, SHUSO_BUF_HEAP, NULL); //TODO use a more efficient buffer memory model
  shuso_io_coro_init(S, &send->fd, dstproc->ipc.socket_transfer_fd[1], SHUSO_IO_WRITE, ipc_send_fd_coroutine, &send->fd_msg_buf);
}

void ipc_receive_notice_coroutine(shuso_t *S, shuso_io_t *io) {
  shuso_process_t    *proc = S->process;
  shuso_io_receive_t *receiver = io->privdata;
  int err;
  
  SHUSO_IO_CORO_BEGIN(io);
  
#ifdef SHUTTLESOCK_HAVE_EVENTFD
  
  do {
    SHUSO_IO_CORO_YIELD(read, &receiver->buf.eventfd, sizeof(receiver->buf.eventfd));
    err = io->error;
  } while(!err && ipc_receive(S, proc));
  
#else
  
  do {
    SHUSO_IO_CORO_YIELD(wait, SHUSO_IO_READ);
    if(!io->error) {
      do {
        //eat up everything in the buffer. we don't care about the contents, only that it must all be consumed
        SHUSO_IO_CORO_YIELD(read_partial, receiver->buf.pipe, sizeof(receiver->buf.pipe));
      } while(io->result > 0);
    }
    
    err = io->error;
    if(err == EAGAIN || err == EWOULDBLOCK) {
      err = 0;
    }
  } while(!err && ipc_receive(S, proc));
  
#endif
  
  SHUSO_IO_CORO_END(io);
  
}

static void ipc_handle_received_socket(shuso_t *S, int fd, uintptr_t ref, void *pd) {
  shuso_ipc_fd_receiver_t *found = NULL;
  for(unsigned i=0; i<S->ipc.fd_receiver.count; i++) {
    if(S->ipc.fd_receiver.array[i].ref == ref) {
      found = &S->ipc.fd_receiver.array[i];
      break;
    }
  }
  assert(fd != -1);
  if(!found) {
    shuso_ipc_fd_receiver_t *reallocd = realloc(S->ipc.fd_receiver.array, sizeof(*reallocd) * (S->ipc.fd_receiver.count+1));
    if(!reallocd) {
      shuso_log_error(S, "failed to receive file descriptor: no memory for realloc()");
      if(fd != -1) {
        close(fd);
      }
      return;
    }
    S->ipc.fd_receiver.array = reallocd;
    found = &reallocd[S->ipc.fd_receiver.count];
    *found = (shuso_ipc_fd_receiver_t) {
      .ref = ref,
      .callback = NULL,
      .pd = NULL,
      .buffered_fds.array = NULL,
      .buffered_fds.count = 0,
      .description = "buffered sockets waiting to be received"
    };
    S->ipc.fd_receiver.count++;
  }
  if(found->callback) {
    found->callback(S, SHUSO_OK, found->ref, fd, pd, found->pd);
  }
  else {
    shuso_ipc_buffered_fd_t  *reallocd = realloc(found->buffered_fds.array, sizeof(*reallocd) * (found->buffered_fds.count+1));
    if(reallocd == NULL) {
      shuso_log_error(S, "failed to receive file descriptor: no memory for buffered fd");
      if(fd != -1) {
        close(fd);
      }
      return;
    }
    
    found->buffered_fds.array = reallocd;
    reallocd[found->buffered_fds.count] = (shuso_ipc_buffered_fd_t ) {.fd = fd, .pd = pd};
    found->buffered_fds.count++;
  }
}

void ipc_receive_msg_fd_coroutine(shuso_t *S, shuso_io_t *io) {
  typedef struct {
    struct msghdr           msg;
    uintptr_t               iov_buf[2];
    struct iovec            iov;
    union {
      //Ancillary data buffer, wrapped in a union in order to ensure it is suitably aligned
      struct cmsghdr          align;
      char                    buf[CMSG_SPACE(sizeof(int))];
    };
  } msghdr_buf_t;
  
  msghdr_buf_t    *msghdr_buf = io->privdata;
  if(!msghdr_buf) {
    msghdr_buf = calloc(1, sizeof(*msghdr_buf));
    io->privdata = msghdr_buf;
  }
  
  struct cmsghdr  *cmsg;
  
  int              fd;
  uintptr_t        ref;
  void            *pd;
  
  SHUSO_IO_CORO_BEGIN(io);
  do {
    
    msghdr_buf->iov = (struct iovec){
      .iov_base = msghdr_buf->iov_buf,
      .iov_len = sizeof(msghdr_buf->iov_buf)
    };
    
    msghdr_buf->msg = (struct msghdr){
      .msg_name = NULL,
      .msg_namelen = 0,
      
      .msg_iov = &msghdr_buf->iov,
      .msg_iovlen = 1,
      
      .msg_control = msghdr_buf->buf,
      .msg_controllen = sizeof(msghdr_buf->buf),
      
      .msg_flags = 0
    };
    
    SHUSO_IO_CORO_YIELD(wait, SHUSO_IO_READ);
    if(io->error) {
      goto end_coroutine;
    }
    SHUSO_IO_CORO_YIELD(recvmsg, &msghdr_buf->msg, 0);
    if(io->error) {
      goto end_coroutine;
    }
    if(io->result == -1) {
      shuso_set_error(S, "recvmsg failed with code (-1)");
      continue;
    }
    cmsg = CMSG_FIRSTHDR(&msghdr_buf->msg);
    if(cmsg == NULL || cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
      shuso_set_error(S, "bad cmsghdr while receiving fd");
      continue;
    }
    else if(cmsg->cmsg_level != SOL_SOCKET) {
      shuso_set_error(S, "bad cmsg_level while receiving fd");
      continue;
    }
    else if(cmsg->cmsg_type != SCM_RIGHTS) {
      shuso_set_error(S, "bad cmsg_type while receiving fd");
      continue;
    }
    
    fd = *((int *)CMSG_DATA(cmsg));
    ref = msghdr_buf->iov_buf[0];
    pd = (void *)msghdr_buf->iov_buf[1];
    ipc_handle_received_socket(S, fd, ref, pd);
  } while(1);
  
end_coroutine:
  if(msghdr_buf) {
    free(msghdr_buf);
    io->privdata = NULL;
  }
  SHUSO_IO_CORO_END(io);
}

bool shuso_ipc_channel_local_init(shuso_t *S) {
  shuso_process_t  *proc = S->process;
  shuso_ev_timer_init(S, &S->ipc.send_retry, 0.0, S->common->config.ipc.send_retry_delay, ipc_send_retry_cb, S->process);
#ifdef SHUTTLESOCK_DEBUG_IPC_RECEIVE_CHECK_TIMER
  shuso_ev_timer_init(S, &S->ipc.receive_check, 1.0, 1.0, ipc_receive_check_cb, S->process);
#endif
  
  S->ipc.fd_receiver.count = 0;
  S->ipc.fd_receiver.array = NULL;
  
int               recv_notice_fd;
#ifdef SHUTTLESOCK_HAVE_EVENTFD
  recv_notice_fd = proc->ipc.fd[1];
#else
  //receiving end of the pipe
  recv_notice_fd = proc->ipc.fd[0];
#endif
  shuso_io_coro_init(S, &S->ipc.io.receive.notice, recv_notice_fd, SHUSO_IO_READ, ipc_receive_notice_coroutine, &S->ipc.io.receive);
  shuso_io_coro_init(S, &S->ipc.io.receive.fd, proc->ipc.socket_transfer_fd[0], SHUSO_IO_READ, ipc_receive_msg_fd_coroutine, NULL);
  
  int out_count;
  if(S->procnum == SHUTTLESOCK_MASTER) {
    //master only talks to the manager directly (well, and to itself..)
    out_count = 2;
  }
  else {
    out_count = 2 + *S->common->process.workers_end;
  }
  S->ipc.io.send = shuso_stalloc(&S->stalloc, sizeof(*S->ipc.io.send) * out_count);
  if(!S->ipc.io.send) {
    shuso_log_error(S, "failed to allocate shuso_io for IPC");
    return false;
  }
  S->ipc.io.send = &S->ipc.io.send[-SHUTTLESOCK_MASTER]; //index it so that worker 0 is at [0]
  
  ipc_channel_local_send_coroutines_init(S, SHUTTLESOCK_MASTER);
  ipc_channel_local_send_coroutines_init(S, SHUTTLESOCK_MANAGER);
  
  if(S->procnum == SHUTTLESOCK_MANAGER || S->procnum >= SHUTTLESOCK_WORKER) {
    for(int i=*S->common->process.workers_start; i<*S->common->process.workers_end; i++) {
      ipc_channel_local_send_coroutines_init(S, i);
    }
  }
  
  return true;
}

bool shuso_ipc_channel_local_start(shuso_t *S) {
  shuso_io_coro_start(&S->ipc.io.receive.notice);
  shuso_io_coro_start(&S->ipc.io.receive.fd);
#ifdef SHUTTLESOCK_DEBUG_IPC_RECEIVE_CHECK_TIMER
  shuso_ev_timer_start(S, &S->ipc.receive_check);
#endif
  return true;
}
bool shuso_ipc_channel_local_stop(shuso_t *S) {
  shuso_io_coro_stop(&S->ipc.io.receive.notice);
  shuso_io_coro_stop(&S->ipc.io.receive.fd);
#ifdef SHUTTLESOCK_DEBUG_IPC_RECEIVE_CHECK_TIMER
  shuso_ev_timer_stop(S, &S->ipc.receive_check);
#endif
  shuso_ipc_handler_t *handler = S->common->ipc_handlers;
  for(shuso_ipc_outbuf_t *cur = S->ipc.buf.first, *next; cur != NULL; cur = next) {
    next = cur->next;
    handler[cur->code].cancel(S, cur->code, cur->ptr);
    free(cur);
  }
  S->ipc.buf.first = NULL;
  S->ipc.buf.last = NULL;
  if(shuso_ev_active(&(S->ipc.send_retry))) {
    shuso_ev_timer_stop(S, &S->ipc.send_retry);
  }
  
  
  for(unsigned i=0; i<S->ipc.fd_receiver.count; i++) {
    shuso_ipc_fd_receiver_t *cur = &S->ipc.fd_receiver.array[i];
    if(cur->callback) {
      cur->callback(S, SHUSO_FAIL, cur->ref, -1, NULL, cur->pd);
    }
    for(unsigned j=0; j<cur->buffered_fds.count; j++) {
      if(cur->buffered_fds.array[j].fd != -1) {
        close(cur->buffered_fds.array[j].fd);
      }
    }
    if(cur->buffered_fds.array) {
      free(cur->buffered_fds.array);
    }
  }
  if(S->ipc.fd_receiver.array) {
    free(S->ipc.fd_receiver.array);
    S->ipc.fd_receiver.count = 0;
  }

  return true;
}

bool shuso_ipc_channel_shared_start(shuso_t *S, shuso_process_t *proc) {
  return true;
}

bool shuso_ipc_channel_shared_stop(shuso_t *S, shuso_process_t *proc) {
  return true;
}

static bool ipc_reserve_write_index(shuso_t *S, shuso_ipc_ringbuf_t *dstbuf, int64_t *index) {
  if(dstbuf->full) {
    return false;
  }
  bool      falseval = false;
  int64_t   next = dstbuf->index.next_write_reserve++; //atomic because next_write_reserve is atomic. Thanks, C11.
  
  int64_t   next_read = dstbuf->index.next_read;
  if(dstbuf->full) {
    //don't decrement next_write_reserve, the reader resets it when it clears the 'full' flag
    return false;
  }
  else if(next - next_read > 255) {
    atomic_compare_exchange_strong(&dstbuf->full, &falseval, true);
    //don't decrement next_write_reserve, the reader resets it when it clears the 'full' flag
    return false;
  }
  
  *index = next;
  return true;
}

static void ipc_release_write_index(shuso_t *S, shuso_ipc_ringbuf_t *dstbuf) {
  atomic_fetch_add(&dstbuf->index.next_write_release, 1);
}

static bool ipc_send_direct(shuso_t *S, shuso_process_t *src, shuso_process_t *dst, const uint8_t code, void *ptr) {
  shuso_ipc_ringbuf_t   *buf = dst->ipc.buf;
  
  int64_t next_write_reserved_index;
  if(!ipc_reserve_write_index(S, buf, &next_write_reserved_index)) {
    //couldn't reserve -- the ringbuf is full or busy
    return false;
  }
  
  assert(next_write_reserved_index >= 0);
  uint8_t next = (uint8_t )next_write_reserved_index;
  
  buf->ptr[next] = ptr;
  buf->code[next] = code;
  ipc_release_write_index(S, buf);
  //shuso_log_debug(S, "ipc_send_direct %p %d fd %d %d", &S->ipc.io.send[dst->procnum].notice, dst->procnum, S->ipc.io.send[dst->procnum].notice.fd, dst->procnum, S->ipc.io.send[dst->procnum].notice.watcher.ev.fd);
  shuso_io_coro_resume(&S->ipc.io.send[dst->procnum].notice);
/*
  ssize_t written;
#ifdef SHUTTLESOCK_HAVE_EVENTFD
  static const uint64_t incr = 1;
  written = write(dst->ipc.fd[1], &incr, sizeof(incr));
  assert(written != -1);
#else
  //just write to the pipe
  written = write(dst->ipc.fd[1], &code, 1);
  assert(written == 1);
#endif
*/
  return true;
}

static bool ipc_send_outbuf_append(shuso_t *S, shuso_process_t *src, shuso_process_t *dst, const uint8_t code, void *ptr) {
  shuso_ipc_channel_local_t  *ch = &S->ipc;
  shuso_ipc_outbuf_t   *buffered = malloc(sizeof(*buffered));
  if(!buffered) {
    return false;
  }
  *buffered = (shuso_ipc_outbuf_t ){
    .code = code,
    .ptr = ptr,
    .dst = dst,
    .next = NULL
  };
  if(!ch->buf.first) {
    ch->buf.first = buffered;
  }
  if(ch->buf.last) {
    ch->buf.last->next = buffered;
  }
  ch->buf.last = buffered;
  if(!shuso_ev_active(&ch->send_retry)) {
    shuso_ev_timer_again(S, &ch->send_retry);
  }
  return true;
}

static bool shuso_ipc_send_proxy_via_manager(shuso_t *S, shuso_process_t *dst, const uint8_t code, void *ptr) {
  shuso_ipc_manager_proxy_msg_t *d = shuso_shared_slab_alloc(&S->common->shm, sizeof(*d));
  if(!d) {
    return shuso_set_error(S, "unable to allocate shared memory for IPC proxy message");
  }
  *d = (shuso_ipc_manager_proxy_msg_t ){
    .code = code,
    .src = S->procnum,
    .dst = dst->procnum,
    .pd = ptr
  };
  return shuso_ipc_send(S, &S->common->process.manager, SHUTTLESOCK_IPC_CMD_MANAGER_PROXY_MESSAGE, d);
}

bool shuso_ipc_send(shuso_t *S, shuso_process_t *dst, const uint8_t code, void *ptr) {
  shuso_process_t         *src =  S->process;
  shuso_runstate_t         dst_state = *dst->state;
  if(dst_state < SHUSO_STATE_STARTING) {
    if(dst->procnum >= SHUTTLESOCK_WORKER) {
      return shuso_set_error(S, "tried sending IPC message to worker %i with state %s", dst->procnum, shuso_runstate_as_string(dst_state));
    }
    else {
      return shuso_set_error(S, "tried sending IPC message to %s with state %s", shuso_process_as_string(dst->procnum), shuso_runstate_as_string(dst_state));
    }
  }
  
  // workers can't communicate directly with the master (and vice versa),
  // so all master<->worker messages must be proxied by the manager
  if((S->procnum == SHUTTLESOCK_MASTER && dst->procnum >= SHUTTLESOCK_WORKER)
   ||(S->procnum >= SHUTTLESOCK_WORKER && dst->procnum == SHUTTLESOCK_MASTER)) {
    return shuso_ipc_send_proxy_via_manager(S, dst, code, ptr);
  }
  
  if(S->ipc.buf.first || dst_state == SHUSO_STATE_STARTING) {
    return ipc_send_outbuf_append(S, src, dst, code, ptr);
  }
  if(!ipc_send_direct(S, src, dst, code, ptr)) {
    return ipc_send_outbuf_append(S, src, dst, code, ptr);
  }
  return true;
}

bool shuso_ipc_send_workers(shuso_t *S, const uint8_t code, void *ptr) {
  unsigned        end = *S->common->process.workers_end;
  shuso_common_t *common = S->common;
  bool            ret = true;
  for(unsigned i=*S->common->process.workers_start; i<end; i++) {
    ret = ret && shuso_ipc_send(S, &common->process.worker[i], code, ptr);
  }
  return ret;
}

static void do_nothing(void) {
  //nothing at all
}

const shuso_ipc_handler_t *shuso_ipc_add_handler(shuso_t *S,  const char *name, uint32_t code, shuso_ipc_fn *receive, shuso_ipc_fn *cancel) {
  if(!shuso_runstate_check(S, SHUSO_STATE_CONFIGURING, "add IPC handler")) {
    return false;
  }
  
  shuso_ipc_handler_t *handlers = S->common->ipc_handlers;
  
  if(code == SHUTTLESOCK_IPC_CODE_AUTOMATIC) {
    for(int i = SHUTTLESOCK_IPC_CODE_AUTOMATIC_MIN; i <= SHUTTLESOCK_IPC_CODE_AUTOMATIC_MAX; i++) {
      if(handlers[i].name == NULL) {
        code = i;
        break;
      }
    }
    if(code == SHUTTLESOCK_IPC_CODE_AUTOMATIC) {
      shuso_set_error(S, "All %d automatic IPC codes are already used.", SHUTTLESOCK_IPC_CODE_AUTOMATIC_MAX - SHUTTLESOCK_IPC_CODE_AUTOMATIC_MIN);
      return NULL;
    }
  }
  
  if(handlers[code].name != NULL) {
    //this code is already handled
    shuso_set_error(S, "IPC code %d is already in use by %s", (int)code, handlers[code].name);
    return NULL;
  }
  handlers[code] = (shuso_ipc_handler_t ){
    .code = code,
    .name = name ? name : "(unnamed)",
    .receive = receive,
    .cancel = cancel ? cancel : (shuso_ipc_fn *)&do_nothing
  };
  return &handlers[code];
}

static void ipc_send_retry_cb(shuso_loop *loop, shuso_ev_timer *w, int revents) {
  shuso_t            *S = shuso_state(loop, w);
  shuso_process_t    *proc = shuso_ev_data(w);
  shuso_ipc_outbuf_t *cur;
  while((cur = S->ipc.buf.first) != NULL) {
    if(!ipc_send_direct(S, proc, cur->dst, cur->code, cur->ptr)) {
      //send still fails. retry again later
      shuso_ev_timer_again(S, w);
      return;
    }
    S->ipc.buf.first = cur->next;
    free(cur);
  }
  S->ipc.buf.last = NULL;
}

static bool ipc_receive(shuso_t *S, shuso_process_t *proc) {
  shuso_ipc_ringbuf_t  *in = proc->ipc.buf;
  uint_fast8_t          code;
  void                 *ptr;
  int64_t               read_index;
  read_index = in->index.next_read;
  assert(read_index >= 0);
  while(read_index < in->index.next_write_release) {
    uint8_t i = (uint8_t )read_index;
    code = in->code[i];
    ptr = in->ptr[i];
    if(code == SHUTTLESOCK_IPC_CMD_NIL) {
      shuso_log_error(S, "ipc: [%d] has nil code -- retry it.", (int )read_index);
    }
    else {
      in->code[i]=SHUTTLESOCK_IPC_CMD_NIL;
      read_index++;
      atomic_fetch_add(&in->index.next_read, 1);
      if(S->common->ipc_handlers[code].receive == (shuso_ipc_fn *)&do_nothing) {
        shuso_log_error(S, "ipc: [%d] isn't handled, do nothing.", (int )code);
      }
      S->common->ipc_handlers[code].receive(S, code, ptr);
    }
  }
  if(in->full) {
    in->index.next_write_reserve = in->index.next_read;
    assert(in->index.next_write_reserve == in->index.next_write_release);
    in->full = false;
  }
  return true;
}

#ifdef SHUTTLESOCK_DEBUG_IPC_RECEIVE_CHECK_TIMER
static void ipc_receive_check_cb(shuso_loop *loop, shuso_ev_timer *w, int revents) {
  shuso_t                   *S = shuso_state(loop, w);
  shuso_ipc_ringbuf_t  *in = S->process->ipc.buf;
  //shuso_log_debug(S, "CHECKPLZ %p full: %d next_read: %d next_write_release: %d", (void *)S->process, (int)in->full, (int )in->index.next_read, (int )in->index.next_write_release);
  if(in->index.next_read < in->index.next_write_release) {
    shuso_log_warning(S, "periodic check revealed %d unread IPC messages", (int )(in->index.next_write_release - in->index.next_read));
    ipc_receive(S, S->process);
  }
}
#endif

static void ipc_receive_timeout_cb(shuso_loop *loop, shuso_ev_timer *w, int revents) {
  shuso_t                   *S = shuso_state(loop, w);
  shuso_ipc_fd_receiver_t   *cur = shuso_ev_data(w);
  
  if(cur->callback) {
    cur->callback(S, SHUSO_TIMEOUT, cur->ref, -1, NULL, cur->pd);
  }
  shuso_ipc_receive_fd_finish(S, cur->ref);
}

bool shuso_ipc_send_fd(shuso_t *S, shuso_process_t *dst_proc, int fd, uintptr_t ref, void *pd) {
  uintptr_t buf[2] = {ref, (uintptr_t )pd};
  
  shuso_io_send_t  *send = &S->ipc.io.send[dst_proc->procnum];
  char             *iov_bufspace = shuso_buffer_add_msg_fd(S, &send->fd_msg_buf, fd, sizeof(buf));
  if(!iov_bufspace) {
    return shuso_set_error(S, "failed to send fd: no space for fd buffer link");
  }
  memcpy(iov_bufspace, buf, sizeof(buf));
  
  shuso_io_coro_resume(&send->fd);
  
  return true;
}

bool shuso_ipc_receive_fd_start(shuso_t *S, const char *description, float timeout_sec, shuso_ipc_receive_fd_fn *callback, uintptr_t ref, void *pd) {
  shuso_ipc_fd_receiver_t   *found = NULL;
  for(unsigned i = 0; i<S->ipc.fd_receiver.count; i++) {
    shuso_ipc_fd_receiver_t *cur = &S->ipc.fd_receiver.array[i];
    if(cur->ref == ref) {
      found = cur;
      break;
    }
  }
  if(found) {
    if(found->callback != NULL) {
      return shuso_set_error(S, "ipc_receive_fd_start ref already exists, has already been started");
    }
    found->callback = callback;
    found->pd = pd;
    found->description = description;
    if(shuso_ev_active(&found->timeout)) {
      shuso_ev_timer_stop(S, &found->timeout);
    }
    if(timeout_sec > 0) {
      shuso_ev_timer_init(S, &found->timeout, timeout_sec, 0, ipc_receive_timeout_cb, found);
      shuso_ev_timer_start(S, &found->timeout);
    }
    
    found->in_use = true;
    for(unsigned j=0; j < found->buffered_fds.count && !found->finished; j++) {
      callback(S, SHUSO_OK, ref, found->buffered_fds.array[j].fd, found->buffered_fds.array[j].pd, pd);
    }
    found->in_use = false;
    if(!found->finished) {
      //got through these buffered fds, but the user expects more, so don't destroy the whole thing yet.
      //just free the buffer
      free(found->buffered_fds.array);
      found->buffered_fds.array = NULL;
      found->buffered_fds.count = 0;
    }
    else {
      shuso_ipc_receive_fd_finish(S, found->ref);
    }
  }
  else {
    shuso_ipc_fd_receiver_t *reallocd = realloc(S->ipc.fd_receiver.array, sizeof(shuso_ipc_fd_receiver_t) * (S->ipc.fd_receiver.count + 1));
    if(!reallocd) {
      return shuso_set_error(S, "ipc_receive_fd_start ref failed, no memory for realloc()");
    }
    reallocd[S->ipc.fd_receiver.count] = (shuso_ipc_fd_receiver_t) {
      .ref = ref,
      .callback = callback,
      .pd = pd,
      .buffered_fds.array = NULL,
      .buffered_fds.count = 0,
      .description = description ? description : "?",
      .in_use = false,
      .finished = false
    };
    S->ipc.fd_receiver.array = reallocd;
    S->ipc.fd_receiver.count++;
  }
  return true;
}

bool shuso_ipc_receive_fd_finish(shuso_t *S, uintptr_t ref) {
  shuso_ipc_fd_receiver_t *found = NULL;
  for(unsigned i = 0; i<S->ipc.fd_receiver.count; i++) {
    if(found) {
      S->ipc.fd_receiver.array[i-1] = S->ipc.fd_receiver.array[i];
    }
    if(S->ipc.fd_receiver.array[i].ref == ref) {
      found = &S->ipc.fd_receiver.array[i];
      if(found->in_use) {
        //mark it for later deallocation
        found->finished = true;
        return true;
      }
    }
  }
  if(!found) {
    shuso_set_error(S, "no ipc fd receiver found with reference %p", (char *)ref);
    return false;
  }
  
  for(unsigned j = 0; j<found->buffered_fds.count; j++) {
    close(found->buffered_fds.array[j].fd);
  }
  if(found->buffered_fds.array){
    free(found->buffered_fds.array);
    found->buffered_fds.array = NULL;
    found->buffered_fds.count = 0;
  }
  if(shuso_ev_active(&found->timeout)) {
    shuso_ev_timer_stop(S, &found->timeout);
  }
  if(S->ipc.fd_receiver.count == 1) {
    free(S->ipc.fd_receiver.array);
    S->ipc.fd_receiver.array = NULL;
    S->ipc.fd_receiver.count = 0;
  }
  else {
    shuso_ipc_fd_receiver_t *reallocd = realloc(S->ipc.fd_receiver.array, sizeof(shuso_ipc_fd_receiver_t) * (S->ipc.fd_receiver.count-1));
    if(!reallocd) {
      //this is not terrible, and will not lead to undefined behavior. don't error out, just make a note of it
      shuso_log_error(S, "ipc_receive_fd_finish failed, no memory for shrinking realloc(). this isn't fatal, continue anyway.");
    }
    else {
      S->ipc.fd_receiver.array = reallocd;
    }
    S->ipc.fd_receiver.count--;
  }
  return true;
}
