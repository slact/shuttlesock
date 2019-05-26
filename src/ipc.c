#include <shuttlesock.h>
#include <shuttlesock/ipc.h>
#include "shuttlesock_private.h"
#include <sys/mman.h>
#include <stdlib.h>

static void ipc_send_retry_cb(EV_P_ ev_timer *w, int revents);
static void ipc_receive_retry_cb(EV_P_ ev_timer *w, int revents);
static void ipc_receive_cb(EV_P_ ev_async *w, int revents);

bool shuso_ipc_channel_shared_create(shuso_t *ctx, shuso_process_t *proc) {
  int               procnum = process_to_procnum(ctx, proc);
  char             *ptr;
  size_t            bufsize = ctx->common->config.ipc_buffer_size;
  size_t            sz = (sizeof(void *) + sizeof(char)) * bufsize;
  
  if(procnum == SHUTTLESOCK_MASTER || procnum == SHUTTLESOCK_MANAGER) {
    ptr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED,-1, 0);
  }
  else {
    ptr = calloc(1, sz);
  }
  if(!ptr) return true;
  proc->ipc.buf.sz = sz;
  proc->ipc.buf.ptr = (void *)ptr;
  proc->ipc.buf.code = (void *)&ptr[sizeof(void *) * bufsize];
  
  ev_async_init(&proc->ipc.receive, ipc_receive_cb);
  proc->ipc.receive.data = ctx->process;
  
  return true;
}

bool shuso_ipc_channel_shared_destroy(shuso_t *ctx, shuso_process_t *proc) {
  int               procnum = process_to_procnum(ctx, proc);
  
  if(procnum == SHUTTLESOCK_MASTER || procnum == SHUTTLESOCK_MANAGER) {
    munmap(proc->ipc.buf.ptr, proc->ipc.buf.sz);
  }
  else {
    free(proc->ipc.buf.ptr);
  }
  proc->ipc.buf.sz = 0;
  proc->ipc.buf.ptr = NULL;
  proc->ipc.buf.code = NULL;
  
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
    ev_timer_stop(ctx->loop, &ctx->ipc.send_retry);
  }
  if(ev_is_active(&ctx->ipc.receive_retry) || ev_is_pending(&ctx->ipc.receive_retry)) {
    ev_timer_stop(ctx->loop, &ctx->ipc.receive_retry);
  }
  return true;
}

bool shuso_ipc_channel_shared_start(shuso_t *ctx, shuso_process_t *proc) {
  ev_async_start(ctx->loop, &proc->ipc.receive);
  return true;
}

bool shuso_ipc_channel_shared_stop(shuso_t *ctx, shuso_process_t *proc) {
  ev_async_stop(ctx->loop, &proc->ipc.receive);
  return true;
}

static bool ipc_send_direct(shuso_t *ctx, shuso_process_t *src, shuso_process_t *dst, const uint8_t code, void *ptr) {
  shuso_ipc_inbuf_t   *buf = &dst->ipc.buf;
  if(buf->last_reserve >= buf->sz-1) {
    //out of space
    return false;
  }
  size_t last = atomic_fetch_add(&buf->last_reserve, 1) + 1;
  buf->ptr[last] = ptr;
  buf->code[last] = code;
  atomic_fetch_add(&buf->last_release, 1);
  ev_async_send(ctx->loop, &dst->ipc.receive);
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
    ev_timer_again(ctx->loop, &ch->send_retry);
  }
  return true;
}

bool shuso_ipc_send(shuso_t *ctx, shuso_process_t *dst, const uint8_t code, void *ptr) {
  shuso_process_t *src = ctx->process;
  if(src->ipc.buf.first) {
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
      ev_timer_again(ctx->loop, w);
      return;
    }
    ctx->ipc.buf.first = cur->next;
    free(cur);
  }
  ctx->ipc.buf.last = NULL;
}

static void ipc_receive(shuso_t *ctx, shuso_process_t *proc) {
  shuso_ipc_inbuf_t  *in = &proc->ipc.buf;
  size_t              last_reserve = in->last_reserve, last_release = in->last_release;
  uint_fast8_t        code;
  void               *ptr;
  for(size_t i=in->first; i<=last_release; i++) {
    code = in->code[i];
    if(!code) {
      //this ipc alert is not ready yet. it will be ready really soon though. retry quite rather very soon
      if(!ev_is_active(&ctx->ipc.receive_retry) && !ev_is_pending(&ctx->ipc.receive_retry)) {
        ev_timer_again(ctx->loop, &ctx->ipc.receive_retry);
      }
      return;
    }
    ptr = in->ptr[i];
    ctx->common->ipc_handlers[code].receive(ctx, code, ptr);
    in->code[i]=0;
    in->first++;
  }
  //alright, we've walked the whole inbound array, now rewind it
  
  //don't just set last_reserve and last_release to 0 because they
  //may have been modified in another thread.
  atomic_fetch_sub(&in->last_reserve, last_reserve);
  atomic_fetch_sub(&in->last_release, last_release);
  //no one else may modify ->first though.
  in->first = 0;
}

static void ipc_receive_retry_cb(EV_P_ ev_timer *w, int revents) {
  shuso_t            *ctx = ev_userdata(EV_A);
  shuso_process_t    *proc = w->data;
  ipc_receive(ctx, proc);
}

static void ipc_receive_cb(EV_P_ ev_async *w, int revents) {
  shuso_t            *ctx = ev_userdata(EV_A);
  shuso_process_t    *proc = w->data;
  ipc_receive(ctx, proc);
}
