#include <shuttlesock.h>
#include <shuttlesock/ipc.h>
#include <shuttlesock/log.h>
#include "shuttlesock_private.h"
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#ifdef SHUTTLESOCK_HAVE_EVENTFD
#include <sys/eventfd.h>
#endif

static void ipc_send_retry_cb(EV_P_ ev_timer *w, int revents);
static void ipc_receive_retry_cb(EV_P_ ev_timer *w, int revents);
static void ipc_receive_cb(EV_P_ ev_io *w, int revents);

bool shuso_ipc_channel_shared_create(shuso_t *ctx, shuso_process_t *proc) {
  int               procnum = process_to_procnum(ctx, proc);
  char             *ptr;
  size_t            bufsize = ctx->common->config.ipc_buffer_size;
  size_t            sz = sizeof(shuso_ipc_inbuf_t) + (sizeof(void *) + sizeof(char)) * bufsize;
  
  if(sz == 0) return false;
  
  if(procnum == SHUTTLESOCK_MASTER || procnum == SHUTTLESOCK_MANAGER) {
    ptr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED,-1, 0);
  }
  else {
    ptr = calloc(1, sz);
  }
  if(!ptr) return false;
  proc->ipc.buf = (void *)ptr;
  proc->ipc.buf->sz = sz;
  
  ptr = (char *)&proc->ipc.buf[1];
  proc->ipc.buf->ptr = (void *)ptr;
  proc->ipc.buf->code = (void *)&ptr[sizeof(void *) * bufsize];
  
  int fds[2]={-1, -1};
#ifdef SHUTTLESOCK_HAVE_EVENTFD
  //straight out of libev
  fds[1] = eventfd(0, 0);
  if(fds[1] == -1) {
    return set_error(ctx, "failed to create IPC channel eventfd");
  }
#else
  if(pipe(fds) == -1) {
    return set_error(ctx, "failed to create IPC channel pipe");
  }
#endif
  proc->ipc.fd[0] = fds[0];
  proc->ipc.fd[1] = fds[1];
  shuso_log(ctx, "fds: %d %d, -> %d %d", fds[0], fds[1], proc->ipc.fd[0], proc->ipc.fd[1]);
  
  fcntl(fds[1], F_SETFL, O_NONBLOCK);
  if(fds[0] != -1) { //using pipe()
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    ev_io_init(&proc->ipc.receive, ipc_receive_cb, fds[0], EV_READ);
  }
  else { //using eventfd
    ev_io_init(&proc->ipc.receive, ipc_receive_cb, fds[1], EV_READ);
  }
  
  proc->ipc.receive.data = ctx->process;
  
  return true;
}

bool shuso_ipc_channel_shared_destroy(shuso_t *ctx, shuso_process_t *proc) {
  int               procnum = process_to_procnum(ctx, proc);
  
  if(procnum == SHUTTLESOCK_MASTER || procnum == SHUTTLESOCK_MANAGER) {
    munmap(proc->ipc.buf, proc->ipc.buf->sz);
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
  return true;
}

bool shuso_ipc_channel_local_init(shuso_t *ctx) {
  ev_timer_init(&ctx->ipc.send_retry, ipc_send_retry_cb, 0.0, ctx->common->config.ipc_send_retry_delay);
  ctx->ipc.send_retry.data = ctx->process;
  
  ev_timer_init(&ctx->ipc.receive_retry, ipc_receive_retry_cb, 0.0, ctx->common->config.ipc_receive_retry_delay);
  ctx->ipc.receive_retry.data = ctx->process;
  return true;
}

bool shuso_ipc_channel_local_start(shuso_t *ctx) {
  //nothing to do
  return true;
}
bool shuso_ipc_channel_local_stop(shuso_t *ctx) {
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
  if(ev_is_active(&ctx->ipc.receive_retry) || ev_is_pending(&ctx->ipc.receive_retry)) {
    ev_timer_stop(ctx->ev.loop, &ctx->ipc.receive_retry);
  }
  return true;
}

bool shuso_ipc_channel_shared_start(shuso_t *ctx, shuso_process_t *proc) {
  proc->ipc.receive.data = proc;
  ev_io_start(ctx->ev.loop, &proc->ipc.receive);
  shuso_log(ctx, "started shared channel, fds %d %d", proc->ipc.fd[0], proc->ipc.fd[1]);
  return true;
}

bool shuso_ipc_channel_shared_stop(shuso_t *ctx, shuso_process_t *proc) {
  ev_io_stop(ctx->ev.loop, &proc->ipc.receive);
  return true;
}

static bool ipc_send_direct(shuso_t *ctx, shuso_process_t *src, shuso_process_t *dst, const uint8_t code, void *ptr) {
  shuso_log(ctx, "direct send to dst %p", (void *)dst);
  shuso_ipc_inbuf_t   *buf = dst->ipc.buf;
  ssize_t              written;
  if(buf->next_reserve >= buf->sz-1) {
    //out of space
    return false;
  }
  size_t next = atomic_fetch_add(&buf->next_reserve, 1);
  buf->ptr[next] = ptr;
  buf->code[next] = code;
  atomic_fetch_add(&buf->next_release, 1);
  shuso_log(ctx, "next_reserve %zd next_release %zd", buf->next_reserve, buf->next_release);
#ifdef SHUTTLESOCK_HAVE_EVENTFD
  shuso_log(ctx, "write to eventfd %d %d", dst->ipc.fd[0], dst->ipc.fd[1]);
  static const uint64_t incr = 1;
  written = write(dst->ipc.fd[1], &incr, sizeof(incr));
  assert(written != -1);
#else
  shuso_log(ctx, "write to pipe %d %d", dst->ipc.fd[0], dst->ipc.fd[1]);
  //just write to the pipe
  written = write(dst->ipc.fd[1], code, 1);
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
  if(src->ipc.buf->first) {
    return ipc_send_outbuf_append(ctx, src, dst, code, ptr);
  }
  if(!ipc_send_direct(ctx, src, dst, code, ptr)) {
    return ipc_send_outbuf_append(ctx, src, dst, code, ptr);
  }
  return true;
}

static void do_nothing(void) {
  //nothing at all
}

bool shuso_ipc_add_handler(shuso_t * ctx,  const char *name, const uint8_t code, shuso_ipc_receive_fn *receive, shuso_ipc_cancel_fn *cancel) {
  shuso_ipc_handler_t *handlers = ctx->common->ipc_handlers;
  if(handlers[code].name != NULL) {
    //this code is already handled
    return false;
  }
  handlers[code] = (shuso_ipc_handler_t ){
    .code = code,
    .name = name ? name : "unnamed",
    .receive = receive,
    .cancel = cancel ? cancel : (shuso_ipc_cancel_fn *)&do_nothing
  };
  return true;
}

static void ipc_send_retry_cb(EV_P_ ev_timer *w, int revents) {
  shuso_t            *ctx = ev_userdata(EV_A);
  shuso_process_t    *proc = w->data;
  shuso_ipc_outbuf_t *cur;
  while((cur = ctx->ipc.buf.first) != NULL) {
    if(!ipc_send_direct(ctx, proc, cur->dst, cur->code, cur->ptr)) {
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
  shuso_ipc_inbuf_t  *in = proc->ipc.buf;
  size_t              next_reserve = in->next_reserve, next_release = in->next_release;
  uint_fast8_t        code;
  void               *ptr;
  shuso_log(ctx, "ipc_receive at dst %p", (void *)proc);
  shuso_log(ctx, "next_reserve %zd next_release %zd", in->next_reserve, in->next_release);
  for(size_t i=in->first; i<next_release; i++) {
    code = in->code[i];
    if(!code) {
      shuso_log(ctx, "ipc_receive no code -- wait a little");
      //this ipc alert is not ready yet. it will be ready really soon though. retry quite rather very soon
      if(!ev_is_active(&ctx->ipc.receive_retry) && !ev_is_pending(&ctx->ipc.receive_retry)) {
        ev_timer_again(ctx->ev.loop, &ctx->ipc.receive_retry);
      }
      return;
    }
    ptr = in->ptr[i];
    shuso_log(ctx, "got code %i", (int )code);
    ctx->common->ipc_handlers[code].receive(ctx, code, ptr);
    in->code[i]=0;
    in->first++;
  }
  //alright, we've walked the whole inbound array, now rewind it
  
  //don't just set next_reserve and next_release to 0 because they
  //may have been modified in another thread.
  atomic_fetch_sub(&in->next_reserve, next_reserve);
  atomic_fetch_sub(&in->next_release, next_release);
  //no one else may modify ->first though.
  in->first = 0;
}

static void ipc_receive_retry_cb(EV_P_ ev_timer *w, int revents) {
  shuso_t            *ctx = ev_userdata(EV_A);
  shuso_process_t    *proc = w->data;
  ipc_receive(ctx, proc);
}

static void ipc_receive_cb(EV_P_ ev_io *w, int revents) {
  shuso_t            *ctx = ev_userdata(EV_A);
  shuso_process_t    *proc = w->data;
  
#ifdef SHUTTLESOCK_HAVE_EVENTFD
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
