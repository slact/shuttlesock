#include <shuttlesock.h>
#include <shuttlesock/ipc.h>
#include <shuttlesock/log.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <shuttlesock/shared_slab.h>
#include <ancillary.h>
#include <errno.h>
#ifdef SHUTTLESOCK_USE_EVENTFD
#include <sys/eventfd.h>
#endif

static void ipc_send_retry_cb(EV_P_ ev_timer *w, int revents);
static void ipc_receive_cb(EV_P_ ev_io *w, int revents);
static void ipc_socket_transfer_receive_cb(EV_P_ ev_io *w, int revents);

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
  shuso_log(ctx, "created socket_transfer_fd %p %d<=>%d", (void *)&proc->ipc.socket_transfer_fd, proc->ipc.socket_transfer_fd[0], proc->ipc.socket_transfer_fd[1]);
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
  ev_timer_init(&ctx->ipc.send_retry, ipc_send_retry_cb, 0.0, ctx->common->config.ipc.send_retry_delay);
  ctx->ipc.send_retry.data = ctx->process;
  ctx->ipc.receive.data = ctx->process;
  ctx->ipc.socket_transfer_receive.data = ctx->process;
  ctx->ipc.fd_receiver.count = 0;
  ctx->ipc.fd_receiver.array = NULL;
  
#ifdef SHUTTLESOCK_USE_EVENTFD
  ev_io_init(&ctx->ipc.receive, ipc_receive_cb, proc->ipc.fd[1], EV_READ);
#else
  ev_io_init(&ctx->ipc.receive, ipc_receive_cb, proc->ipc.fd[0], EV_READ);
#endif
  
  ev_io_init(&ctx->ipc.socket_transfer_receive, ipc_socket_transfer_receive_cb, proc->ipc.socket_transfer_fd[0], EV_READ);
  return true;
}

bool shuso_ipc_channel_local_start(shuso_t *ctx) {
  //nothing to do
  ev_io_start(ctx->ev.loop, &ctx->ipc.receive);
  ev_io_start(ctx->ev.loop, &ctx->ipc.socket_transfer_receive);
  return true;
}
bool shuso_ipc_channel_local_stop(shuso_t *ctx) {
  ev_io_stop(ctx->ev.loop, &ctx->ipc.receive);
  ev_io_stop(ctx->ev.loop, &ctx->ipc.socket_transfer_receive);
  
  shuso_ipc_handler_t *handler = ctx->common->ipc_handlers;
  for(shuso_ipc_outbuf_t *cur = ctx->ipc.buf.first, *next; cur != NULL; cur = next) {
    next = cur->next;
    handler[cur->code].cancel(ctx, cur->code, cur->ptr);
    free(cur);
  }
  ctx->ipc.buf.first = NULL;
  ctx->ipc.buf.last = NULL;
  if(ev_is_active(&ctx->ipc.send_retry) || ev_is_pending(&ctx->ipc.send_retry)) {
    ev_timer_stop(ctx->ev.loop, &ctx->ipc.send_retry);
  }
  
  
  for(unsigned i=0; i<ctx->ipc.fd_receiver.count; i++) {
    shuso_ipc_fd_receiver_t *cur = &ctx->ipc.fd_receiver.array[i];
    if(cur->callback) {
      cur->callback(ctx, false, cur->ref, -1, NULL, cur->pd);
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
  if(!ev_is_active(&ch->send_retry) && !ev_is_pending(&ch->send_retry)) {
    ev_timer_again(ctx->ev.loop, &ch->send_retry);
  }
  return true;
}

bool shuso_ipc_send(shuso_t *ctx, shuso_process_t *dst, const uint8_t code, void *ptr) {
  shuso_process_t *src = ctx->process;
  //shuso_log(ctx, "ipc send code %d ptr %p", (int )code, ptr);
  assert(*dst->state >= SHUSO_PROCESS_STATE_RUNNING);
  if(ctx->ipc.buf.first) {
    //shuso_log(ctx, "inbuf appears full from the start...");
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

static void ipc_send_retry_cb(EV_P_ ev_timer *w, int revents) {
  shuso_t            *ctx = ev_userdata(EV_A);
  shuso_process_t    *proc = w->data;
  shuso_ipc_outbuf_t *cur;
  while((cur = ctx->ipc.buf.first) != NULL) {
    //shuso_log(ctx, "retry send");
    if(!ipc_send_direct(ctx, proc, cur->dst, cur->code, cur->ptr)) {
      //shuso_log(ctx, "retry send still fails");
      //send still fails. retry again later
      ev_timer_again(ctx->ev.loop, w);
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


bool shuso_ipc_send_fd(shuso_t *ctx, shuso_process_t *dst_proc, int fd, uintptr_t ref, void *pd) {
  uintptr_t buf[2] = {ref, (uintptr_t )pd};
  return ancil_send_fd(dst_proc->ipc.socket_transfer_fd[1], fd, (const char *)buf, sizeof(buf)) == 0;
}

bool shuso_ipc_receive_fd_start(shuso_t *ctx, const char *description, float timeout_msec, shuso_ipc_receive_fd_fn *callback, uintptr_t ref, void *pd) {
  for(unsigned i = 0; i<ctx->ipc.fd_receiver.count; i++) {
    shuso_ipc_fd_receiver_t *cur = &ctx->ipc.fd_receiver.array[i];
    if(cur->ref == ref) {
      if(cur->callback != NULL) {
        shuso_log(ctx, "ipc_receive_fd_start ref already exists, has already been started");
        return false;
      }
      cur->callback = callback;
      cur->pd = pd;
      cur->description = description;
      
      for(unsigned j=0; j < cur->buffered_fds.count; j++) {
        callback(ctx, true, ref, cur->buffered_fds.array[j].fd, cur->buffered_fds.array[j].pd, pd);
      }
      free(cur->buffered_fds.array);
      cur->buffered_fds.array = NULL;
      cur->buffered_fds.count = 0;
    }
  }
  shuso_ipc_fd_receiver_t *reallocd = realloc(ctx->ipc.fd_receiver.array, sizeof(shuso_ipc_fd_receiver_t) * ctx->ipc.fd_receiver.count + 1);
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
    .description = description ? description : "?"
  };
  ctx->ipc.fd_receiver.array = reallocd;
  ctx->ipc.fd_receiver.count++;
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
      for(unsigned j = 0; j<found->buffered_fds.count; j++) {
        close(found->buffered_fds.array[j].fd);
      }
      if(found->buffered_fds.array) free(found->buffered_fds.array);
    }
  }
  if(!found) {
    return false;
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

static void ipc_receive_cb(EV_P_ ev_io *w, int revents) {
  shuso_t            *ctx = ev_userdata(EV_A);
  shuso_process_t    *proc = w->data;
  
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

static void ipc_socket_transfer_receive_cb(EV_P_ ev_io *w, int revents) {
  shuso_t            *ctx = ev_userdata(EV_A);
  shuso_process_t    *proc = w->data;
  uintptr_t databuf[2];
  uintptr_t ref;
  void     *pd;
  size_t    data_received_sz;
  int       fd = -1;
  int       rc;
  while((rc = ancil_recv_fd(proc->ipc.socket_transfer_fd[0], &fd, (char *)databuf, sizeof(databuf), &data_received_sz)) != -1) {
    if(data_received_sz != sizeof(databuf)) {
      shuso_log(ctx, "failed to receive file descriptor: incorrect data size");
      continue;
    }
    ref = databuf[0];
    pd = (void *)databuf[1];
    
    shuso_ipc_fd_receiver_t *found = NULL;
    for(unsigned i=0; i<ctx->ipc.fd_receiver.count; i++) {
      if(ctx->ipc.fd_receiver.array[i].ref == ref) {
        found = &ctx->ipc.fd_receiver.array[i];
        break;
      }
    }
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
      found->callback(ctx, true, found->ref, fd, pd, found->pd);
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
