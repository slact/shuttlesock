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
#ifdef SHUTTLESOCK_USE_EVENTFD
#include <sys/eventfd.h>
#endif
static void ipc_send_retry_cb(shuso_loop *loop, shuso_ev_timer *w, int revents);
static void ipc_receive_cb(shuso_loop *loop, shuso_ev_io *w, int revents);
static void ipc_socket_transfer_receive_cb(shuso_loop *loop, shuso_ev_io *w, int revents);

bool shuso_ipc_channel_shared_create(shuso_t *ctx, shuso_process_t *proc) {
  int               procnum = shuso_process_to_procnum(ctx, proc);
  void             *ptr;
  
  if(procnum == SHUTTLESOCK_MASTER || procnum == SHUTTLESOCK_MANAGER) {
    ptr = mmap(NULL, sizeof(shuso_ipc_ringbuf_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED,-1, 0);
  }
  else {
    ptr = calloc(1, sizeof(shuso_ipc_ringbuf_t));
  }
  if(!ptr) return false;
  proc->ipc.buf = ptr;
  
  int fds[2]={-1, -1};
#ifdef SHUTTLESOCK_USE_EVENTFD
  //straight out of libev
  fds[1] = eventfd(0, 0);
  if(fds[1] == -1) {
    return shuso_set_error(ctx, "failed to create IPC channel eventfd");
  }
  fcntl(fds[1], F_SETFL, O_NONBLOCK);
#else
  if(pipe(fds) == -1) {
    return shuso_set_error(ctx, "failed to create IPC channel pipe");
  }
  fcntl(fds[0], F_SETFL, O_NONBLOCK);
  fcntl(fds[1], F_SETFL, O_NONBLOCK);
#endif
  proc->ipc.fd[0] = fds[0];
  proc->ipc.fd[1] = fds[1];
  //shuso_log(ctx, "created shared IPC channel fds: %d %d", fds[0], fds[1]);
  
  //open socket-transfer sockets
  if((socketpair(PF_LOCAL, SOCK_STREAM, 0, proc->ipc.socket_transfer_fd)) == -1) {
    return shuso_set_error(ctx, "failed to create IPC channel socket transfer socket");
  }
  fcntl(proc->ipc.socket_transfer_fd[0], F_SETFL, O_NONBLOCK);
  fcntl(proc->ipc.socket_transfer_fd[1], F_SETFL, O_NONBLOCK);
  //shuso_log(ctx, "created socket_transfer_fd %p %d<=>%d", (void *)&proc->ipc.socket_transfer_fd, proc->ipc.socket_transfer_fd[0], proc->ipc.socket_transfer_fd[1]);
  return true;
}

bool shuso_ipc_channel_shared_destroy(shuso_t *ctx, shuso_process_t *proc) {
  int               procnum = shuso_process_to_procnum(ctx, proc);
  if(procnum == SHUTTLESOCK_MASTER || procnum == SHUTTLESOCK_MANAGER) {
    //shuso_log(ctx, "destroy shared IPC for %s", procnum == SHUTTLESOCK_MASTER ? "master" : "manager");
    munmap(proc->ipc.buf, sizeof(shuso_ipc_ringbuf_t));
  }
  else {
    //shuso_log(ctx, "destroy shared IPC for worker %i", procnum);
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

bool shuso_ipc_channel_local_init(shuso_t *ctx) {
  shuso_process_t  *proc = ctx->process;
  shuso_ev_timer_init(ctx, &ctx->ipc.send_retry, 0.0, ctx->common->config.ipc.send_retry_delay, ipc_send_retry_cb, ctx->process);
  
  ctx->ipc.fd_receiver.count = 0;
  ctx->ipc.fd_receiver.array = NULL;
  
#ifdef SHUTTLESOCK_USE_EVENTFD
  shuso_ev_io_init(ctx, &ctx->ipc.receive, proc->ipc.fd[1], EV_READ, ipc_receive_cb, ctx->process);
#else
  shuso_ev_io_init(ctx, &ctx->ipc.receive, proc->ipc.fd[0], EV_READ, ipc_receive_cb, ctx->process);
#endif
  
  shuso_ev_io_init(ctx, &ctx->ipc.socket_transfer_receive, proc->ipc.socket_transfer_fd[0], EV_READ, ipc_socket_transfer_receive_cb, ctx->process);
  return true;
}

bool shuso_ipc_channel_local_start(shuso_t *ctx) {
  shuso_ev_io_start(ctx, &ctx->ipc.receive);
  shuso_ev_io_start(ctx, &ctx->ipc.socket_transfer_receive);
  return true;
}
bool shuso_ipc_channel_local_stop(shuso_t *ctx) {
  shuso_ev_io_stop(ctx, &ctx->ipc.receive);
  shuso_ev_io_stop(ctx, &ctx->ipc.socket_transfer_receive);
  
  shuso_ipc_handler_t *handler = ctx->common->ipc_handlers;
  for(shuso_ipc_outbuf_t *cur = ctx->ipc.buf.first, *next; cur != NULL; cur = next) {
    next = cur->next;
    handler[cur->code].cancel(ctx, cur->code, cur->ptr);
    free(cur);
  }
  ctx->ipc.buf.first = NULL;
  ctx->ipc.buf.last = NULL;
  if(shuso_ev_active(&ctx->ipc.send_retry)) {
    shuso_ev_timer_stop(ctx, &ctx->ipc.send_retry);
  }
  
  
  for(unsigned i=0; i<ctx->ipc.fd_receiver.count; i++) {
    shuso_ipc_fd_receiver_t *cur = &ctx->ipc.fd_receiver.array[i];
    if(cur->callback) {
      cur->callback(ctx, SHUSO_FAIL, cur->ref, -1, NULL, cur->pd);
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
  if(ctx->ipc.fd_receiver.array) {
    free(ctx->ipc.fd_receiver.array);
    ctx->ipc.fd_receiver.count = 0;
  }

  return true;
}

bool shuso_ipc_channel_shared_start(shuso_t *ctx, shuso_process_t *proc) {
  //shuso_log(ctx, "started shared channel, fds %d %d", proc->ipc.fd[0], proc->ipc.fd[1]);
  return true;
}

bool shuso_ipc_channel_shared_stop(shuso_t *ctx, shuso_process_t *proc) {
  return true;
}

static bool ipc_send_direct(shuso_t *ctx, shuso_process_t *src, shuso_process_t *dst, const uint8_t code, void *ptr) {
  //shuso_log(ctx, "direct send to dst %p", (void *)dst);
  shuso_ipc_ringbuf_t   *buf = dst->ipc.buf;
  ssize_t                written;
  if(buf->next_read == buf->next_reserve && buf->code[buf->next_reserve] != SHUTTLESOCK_IPC_CMD_NIL) {
    //out of space
    return false;
  }
  uint8_t next = atomic_fetch_add(&buf->next_reserve, 1);
  if(buf->next_read == buf->next_reserve && buf->code[buf->next_reserve] != SHUTTLESOCK_IPC_CMD_NIL) {
    //out of space
    //TODO: Is this safe??? seems like it.
    atomic_fetch_sub(&buf->next_reserve, 1);
    return false;
  }
  buf->ptr[next] = ptr;
  //shuso_log(ctx, "write? [%u] ipc code buf %p #%d %p, code %d", (int )next, (void *)buf, next, (void *)&buf->code[next], (int )code);
  buf->code[next] = code;
  //shuso_log(ctx, "write! code at %d %d ptr %d", (int )next, buf->code[next], (int )(intptr_t )buf->ptr[next]);
  atomic_fetch_add(&buf->next_release, 1);
  //shuso_log(ctx, "after write: next_reserve %d next_release %d", (int )buf->next_reserve, (int )buf->next_release);
#ifdef SHUTTLESOCK_USE_EVENTFD
  //shuso_log(ctx, "write to eventfd %d %d", dst->ipc.fd[0], dst->ipc.fd[1]);
  static const uint64_t incr = 1;
  written = write(dst->ipc.fd[1], &incr, sizeof(incr));
  assert(written != -1);
#else
  //shuso_log(ctx, "write to pipe %d %d", dst->ipc.fd[0], dst->ipc.fd[1]);
  //just write to the pipe
  written = write(dst->ipc.fd[1], &code, 1);
  assert(written == 1);
#endif
  return true;
}

static bool ipc_send_outbuf_append(shuso_t *ctx, shuso_process_t *src, shuso_process_t *dst, const uint8_t code, void *ptr) {
  shuso_ipc_channel_local_t  *ch = &ctx->ipc;
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
    shuso_ev_timer_again(ctx, &ch->send_retry);
  }
  return true;
}

bool shuso_ipc_send(shuso_t *ctx, shuso_process_t *dst, const uint8_t code, void *ptr) {
  shuso_process_t *src = ctx->process;
  //shuso_log(ctx, "ipc send code %d ptr %p", (int )code, ptr);
  shuso_process_state_t dst_state = *dst->state;
  if(dst_state < SHUSO_PROCESS_STATE_STARTING) {
    return shuso_set_error(ctx, "tried sending IPC message to dead or nonexistent process");
  }
  if(ctx->ipc.buf.first || dst_state == SHUSO_PROCESS_STATE_STARTING) {
    //shuso_log(ctx, "inbuf appears full from the start or process isn't running");
    return ipc_send_outbuf_append(ctx, src, dst, code, ptr);
  }
  if(!ipc_send_direct(ctx, src, dst, code, ptr)) {
    //shuso_log(ctx, "failed to send via inbuf...");
    return ipc_send_outbuf_append(ctx, src, dst, code, ptr);
  }
  return true;
}

bool shuso_ipc_send_workers(shuso_t *ctx, const uint8_t code, void *ptr) {
  unsigned        end = ctx->common->process.workers_end;
  shuso_common_t *common = ctx->common;
  bool            ret = true;
  for(unsigned i=ctx->common->process.workers_start; i<end; i++) {
    ret = ret && shuso_ipc_send(ctx, &common->process.worker[i], code, ptr);
  }
  return ret;
}

static void do_nothing(void) {
  //nothing at all
}

bool shuso_ipc_add_handler(shuso_t * ctx,  const char *name, const uint8_t code, shuso_ipc_fn *receive, shuso_ipc_fn *cancel) {
  shuso_ipc_handler_t *handlers = ctx->common->ipc_handlers;
  if(handlers[code].name != NULL) {
    //this code is already handled
    return false;
  }
  handlers[code] = (shuso_ipc_handler_t ){
    .code = code,
    .name = name ? name : "unnamed",
    .receive = receive,
    .cancel = cancel ? cancel : (shuso_ipc_fn *)&do_nothing
  };
  return true;
}

static void ipc_send_retry_cb(shuso_loop *loop, shuso_ev_timer *w, int revents) {
  shuso_t            *ctx = shuso_ev_ctx(loop, w);
  shuso_process_t    *proc = shuso_ev_data(w);
  shuso_ipc_outbuf_t *cur;
  while((cur = ctx->ipc.buf.first) != NULL) {
    //shuso_log(ctx, "retry send");
    if(!ipc_send_direct(ctx, proc, cur->dst, cur->code, cur->ptr)) {
      //shuso_log(ctx, "retry send still fails");
      //send still fails. retry again later
      shuso_ev_timer_again(ctx, w);
      return;
    }
    ctx->ipc.buf.first = cur->next;
    free(cur);
  }
  ctx->ipc.buf.last = NULL;
}

static void ipc_receive(shuso_t *ctx, shuso_process_t *proc) {
  shuso_ipc_ringbuf_t  *in = proc->ipc.buf;
  uint_fast8_t          code;
  void                 *ptr;
  uint8_t               i;
  //shuso_log(ctx, "ipc_receive at dst %p", (void *)proc);
  //shuso_log(ctx, "first: %d next_reserve %d next_release %d", (int )in->next_read, (int )in->next_reserve, (int )in->next_release);
  for(i=in->next_read; i!=in->next_release; i++) {
    //shuso_log(ctx, "next_release while reading: %d", (int )in->next_release);
    code = in->code[i];
    ptr = in->ptr[i];
    //shuso_log(ctx, "read! ipc at %d code %d ptr %d", i, (int )code, (int )(intptr_t )ptr);
    in->code[i]=0;
    if(!code) {
      shuso_log(ctx, "ipc: [%d] has nil code -- skip it.", (int )i);
    }
    else {
      ctx->common->ipc_handlers[code].receive(ctx, code, ptr);
    }
  }
  //no one else may modify ->first though.
  in->next_read = i;
}


static void ipc_receive_timeout_cb(shuso_loop *loop, shuso_ev_timer *w, int revents) {
  shuso_t                   *ctx = shuso_ev_ctx(loop, w);
  shuso_ipc_fd_receiver_t   *cur = shuso_ev_data(w);
  
  if(cur->callback) {
    cur->callback(ctx, SHUSO_TIMEOUT, cur->ref, -1, NULL, cur->pd);
  }
  shuso_ipc_receive_fd_finish(ctx, cur->ref);
}

bool shuso_ipc_send_fd(shuso_t *ctx, shuso_process_t *dst_proc, int fd, uintptr_t ref, void *pd) {
  int       n;
  
  uintptr_t buf[2] = {ref, (uintptr_t )pd};
  
  struct iovec iov = {
    .iov_base = buf,
    .iov_len = sizeof(buf)
  };
  
  union {
    //Ancillary data buffer, wrapped in a union in order to ensure it is suitably aligned
    char buf[CMSG_SPACE(sizeof(fd))];
    struct cmsghdr align;
  } ancillary_buf;
  
  struct msghdr msg = {
    .msg_name = NULL,
    .msg_namelen = 0,
    
    .msg_iov = &iov,
    .msg_iovlen = 1,
    
    .msg_control = ancillary_buf.buf,
    .msg_controllen = sizeof(ancillary_buf.buf),
    
    .msg_flags = 0
  };
  
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
  
  memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));
  
  do {
    n = sendmsg(dst_proc->ipc.socket_transfer_fd[1], &msg, 0);
  } while(n == -1 && errno == EINTR);
  return n > 0;
}

bool shuso_ipc_receive_fd_start(shuso_t *ctx, const char *description, float timeout_sec, shuso_ipc_receive_fd_fn *callback, uintptr_t ref, void *pd) {
  shuso_ipc_fd_receiver_t   *found = NULL;
  for(unsigned i = 0; i<ctx->ipc.fd_receiver.count; i++) {
    shuso_ipc_fd_receiver_t *cur = &ctx->ipc.fd_receiver.array[i];
    if(cur->ref == ref) {
      found = cur;
      break;
    }
  }
  if(found) {
    if(found->callback != NULL) {
      shuso_log(ctx, "ipc_receive_fd_start ref already exists, has already been started");
      return false;
    }
    found->callback = callback;
    found->pd = pd;
    found->description = description;
    if(shuso_ev_active(&found->timeout)) {
      shuso_ev_timer_stop(ctx, &found->timeout);
    }
    if(timeout_sec > 0) {
      shuso_ev_timer_init(ctx, &found->timeout, timeout_sec, 0, ipc_receive_timeout_cb, found);
      shuso_ev_timer_start(ctx, &found->timeout);
    }
    
    found->in_use = true;
    for(unsigned j=0; j < found->buffered_fds.count && !found->finished; j++) {
      callback(ctx, SHUSO_OK, ref, found->buffered_fds.array[j].fd, found->buffered_fds.array[j].pd, pd);
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
      shuso_ipc_receive_fd_finish(ctx, found->ref);
    }
  }
  else {
    shuso_ipc_fd_receiver_t *reallocd = realloc(ctx->ipc.fd_receiver.array, sizeof(shuso_ipc_fd_receiver_t) * (ctx->ipc.fd_receiver.count + 1));
    if(!reallocd) {
      shuso_log(ctx, "ipc_receive_fd_start ref failed, no memory for realloc()");
      return false;
    }
    reallocd[ctx->ipc.fd_receiver.count] = (shuso_ipc_fd_receiver_t) {
      .ref = ref,
      .callback = callback,
      .pd = pd,
      .buffered_fds.array = NULL,
      .buffered_fds.count = 0,
      .description = description ? description : "?",
      .in_use = false,
      .finished = false
    };
    ctx->ipc.fd_receiver.array = reallocd;
    ctx->ipc.fd_receiver.count++;
  }
  return true;
}

bool shuso_ipc_receive_fd_finish(shuso_t *ctx, uintptr_t ref) {
  shuso_ipc_fd_receiver_t *found = NULL;
  for(unsigned i = 0; i<ctx->ipc.fd_receiver.count; i++) {
    if(found) {
      ctx->ipc.fd_receiver.array[i-1] = ctx->ipc.fd_receiver.array[i];
    }
    if(ctx->ipc.fd_receiver.array[i].ref == ref) {
      found = &ctx->ipc.fd_receiver.array[i];
      if(found->in_use) {
        //mark it for later deallocation
        found->finished = true;
        return true;
      }
    }
  }
  if(!found) {
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
    shuso_ev_timer_stop(ctx, &found->timeout);
  }
  if(ctx->ipc.fd_receiver.count == 1) {
    free(ctx->ipc.fd_receiver.array);
    ctx->ipc.fd_receiver.array = NULL;
    ctx->ipc.fd_receiver.count = 0;
  }
  else {
    shuso_ipc_fd_receiver_t *reallocd = realloc(ctx->ipc.fd_receiver.array, sizeof(shuso_ipc_fd_receiver_t) * ctx->ipc.fd_receiver.count-1);
    if(!reallocd) {
      shuso_log(ctx, "ipc_receive_fd_finish failed, no memory for shrinking realloc()");
    }
    else {
      ctx->ipc.fd_receiver.array = reallocd;
    }
    ctx->ipc.fd_receiver.count--;
  }
  return true;
}

static void ipc_receive_cb(shuso_loop *loop, shuso_ev_io *w, int revents) {
  shuso_t            *ctx = shuso_ev_ctx(loop, w);
  shuso_process_t    *proc = shuso_ev_data(w);
  
#ifdef SHUTTLESOCK_USE_EVENTFD
  uint64_t buf;
  ssize_t readsize = read(proc->ipc.fd[1], &buf, sizeof(buf));
  if(readsize <= 0) {
    shuso_log(ctx, "ipc_receive callback got eventfd readsize %zd", readsize);
  }
#else
  char buf[32];
  ssize_t readsize = read(proc->ipc.fd[0], buf, sizeof(buf));
  if(readsize <= 0) {
    shuso_log(ctx, "ipc_receive callback got eventfd readsize %zd", readsize);
  }
#endif
  
  ipc_receive(ctx, proc);
}


static shuso_status_t shuso_recv_fd(shuso_t *ctx, int fd_channel, int *fd, uintptr_t *ref, void **pd) {
  int       n;
  uintptr_t buf[2];
  struct iovec iov = {
    .iov_base = buf,
    .iov_len = sizeof(buf)
  };
  
  union {
    //Ancillary data buffer, wrapped in a union in order to ensure it is suitably aligned
    char buf[CMSG_SPACE(sizeof(*fd))];
    struct cmsghdr align;
  } ancillary_buf;
  
  struct msghdr msg = {
    .msg_name = NULL,
    .msg_namelen = 0,
    
    .msg_iov = &iov,
    .msg_iovlen = 1,
    
    .msg_control = ancillary_buf.buf,
    .msg_controllen = sizeof(ancillary_buf.buf),
    
    .msg_flags = 0
  };
  do {
    n = recvmsg(fd_channel, &msg, 0);
  } while(n == -1 && errno == EINTR);
  if(n == 0 || (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
    //nothing to recv
    return SHUSO_DEFERRED;
  }
  else if(n == -1) {
    shuso_set_error(ctx, "recvmsg failed with code (-1)");
    return SHUSO_FAIL;
  }
  assert(n == sizeof(buf));
  
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  if(cmsg == NULL || cmsg->cmsg_len != CMSG_LEN(sizeof(*fd))) {
    shuso_set_error(ctx, "bad cmsghdr while receiving fd");
    return SHUSO_FAIL;
  }
  else if(cmsg->cmsg_level != SOL_SOCKET) {
    shuso_set_error(ctx, "bad cmsg_level while receiving fd");
    return SHUSO_FAIL;
  }
  else if(cmsg->cmsg_type != SCM_RIGHTS) {
    shuso_set_error(ctx, "bad cmsg_type while receiving fd");
    return SHUSO_FAIL;
  }
  *fd = *((int *)CMSG_DATA(cmsg));
  *ref = buf[0];
  *pd = (void *)buf[1];
  return SHUSO_OK;
}

static void ipc_socket_transfer_receive_cb(shuso_loop *loop, shuso_ev_io *w, int revents) {
  shuso_t            *ctx = shuso_ev_ctx(loop, w);
  shuso_process_t    *proc = shuso_ev_data(w);
  uintptr_t ref;
  void     *pd;
  int       fd = -1;
  int       rc;
  while(true) {
    rc = shuso_recv_fd(ctx, proc->ipc.socket_transfer_fd[0], &fd, &ref, &pd);
    if(rc == SHUSO_FAIL) {
      shuso_log(ctx, "failed to receive file descriptor: recvmsg error");
      //TODO: investigate the errno, decide if we should keep looping
      //for now, just bail out
      break;
    }
    else if(rc == SHUSO_DEFERRED) {
      break;
    }
    
    shuso_ipc_fd_receiver_t *found = NULL;
    for(unsigned i=0; i<ctx->ipc.fd_receiver.count; i++) {
      if(ctx->ipc.fd_receiver.array[i].ref == ref) {
        found = &ctx->ipc.fd_receiver.array[i];
        break;
      }
    }
    assert(fd != -1);
    if(!found) {
      shuso_ipc_fd_receiver_t *reallocd = realloc(ctx->ipc.fd_receiver.array, sizeof(shuso_ipc_fd_receiver_t) * (ctx->ipc.fd_receiver.count+1));
      if(!reallocd) {
        shuso_log(ctx, "failed to receive file descriptor: no memory for realloc()");
        if(fd != -1) close(fd);
        continue;
      }
      ctx->ipc.fd_receiver.array = reallocd;
      found = &reallocd[ctx->ipc.fd_receiver.count];
      *found = (shuso_ipc_fd_receiver_t) {
        .ref = ref,
        .callback = NULL,
        .pd = NULL,
        .buffered_fds.array = NULL,
        .buffered_fds.count = 0,
        .description = "buffered sockets waiting to be received"
      };
      ctx->ipc.fd_receiver.count++;
    }
    if(found->callback) {
      found->callback(ctx, SHUSO_OK, found->ref, fd, pd, found->pd);
    }
    else {
      shuso_ipc_buffered_fd_t  *reallocd = realloc(found->buffered_fds.array, sizeof(shuso_ipc_buffered_fd_t) * (found->buffered_fds.count+1));
      if(!reallocd) {
        shuso_log(ctx, "failed to receive file descriptor: no memory for buffered fd");
        if(fd != -1) close(fd);
        continue;
      }
      reallocd[found->buffered_fds.count] = (shuso_ipc_buffered_fd_t ) {.fd = fd, .pd = pd};
      found->buffered_fds.array = reallocd;
      found->buffered_fds.count++;
    }
  }
}
