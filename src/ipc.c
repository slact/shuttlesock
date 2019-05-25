#include <shuttlesock.h>
#include <shuttlesock/ipc.h>
#include "shuttlesock_private.h"
#include <sys/mman.h>
#include <stdlib.h>

static void ipc_send_retry_cb(EV_P_ ev_timer *w, int revents);
static void ipc_receive_retry_cb(EV_P_ ev_timer *w, int revents);
static void ipc_receive_cb(EV_P_ ev_async *w, int revents);

shuso_ipc_channel_t *shuso_ipc_channel_create(shuso_t *ctx, shuso_process_t *proc) {
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
  if(!ptr) return NULL;
  proc->ipc.in.sz = sz;
  proc->ipc.in.ptr = (void *)ptr;
  proc->ipc.in.code = (void *)&ptr[sizeof(void *) * bufsize];
  proc->ipc.out.first = NULL; 
  proc->ipc.out.last = NULL; 
  
  ev_async_init(&proc->ipc.receive, ipc_receive_cb);
  proc->ipc.receive.data = proc;
  
  ev_timer_init(&proc->ipc.send_retry, ipc_send_retry_cb, ctx->common->config.ipc_send_retry_delay, 0.0);
  proc->ipc.send_retry.data = proc;
  
  ev_timer_init(&proc->ipc.receive_retry, ipc_receive_retry_cb, ctx->common->config.ipc_receive_retry_delay, 0.0);
  proc->ipc.receive_retry.data = proc;
  return NULL;
}

bool shuso_ipc_channel_start(shuso_t *ctx, shuso_process_t *proc) {
  ev_async_start(ctx->loop, &proc->ipc.receive);
  return true;
}

bool shuso_ipc_channel_stop(shuso_t *ctx, shuso_process_t *proc) {
  ev_async_stop(ctx->loop, &proc->ipc.receive);
  return true;
}

bool shuso_ipc_channel_destroy(shuso_t *ctx, shuso_process_t *proc) {
  int               procnum = process_to_procnum(ctx, proc);
  
  shuso_ipc_channel_stop(ctx, proc);
  
  if(procnum == SHUTTLESOCK_MASTER || procnum == SHUTTLESOCK_MANAGER) {
    munmap(proc->ipc.in.ptr, proc->ipc.in.sz);
  }
  else {
    free(proc->ipc.in.ptr);
  }
  proc->ipc.in.sz = 0;
  proc->ipc.in.ptr = NULL;
  proc->ipc.in.code = NULL;
  
  for(shuso_ipc_outbuf_t *cur = proc->ipc.out.first, *next; cur != NULL; cur = next) {
    next = cur->next;
    free(cur);
    //TODO: cleanup for cur maybe?
  }
  proc->ipc.out.first = NULL;
  proc->ipc.out.last = NULL;
  
  return true;
}

static bool ipc_send_direct(shuso_t *ctx, shuso_process_t *src, shuso_process_t *dst, const uint8_t code, void *ptr) {
  shuso_ipc_inbuf_t   *buf = &dst->ipc.in;
  if(buf->last_reserve >= buf->sz-1) {
    //out of space
    return false;
  }
  size_t last = atomic_fetch_add(&buf->last_reserve, 1) + 1;
  buf->ptr[last] = (intptr_t )ptr;
  buf->code[last] = code;
  atomic_fetch_add(&buf->last_release, 1);
  ev_async_send(ctx->loop, &dst->ipc.receive);
  return true;
}

static bool ipc_send_outbuf_append(shuso_t *ctx, shuso_process_t *src, shuso_process_t *dst, const uint8_t code, void *ptr) {
  shuso_ipc_channel_t  *ch = &src->ipc;
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
  if(!ch->out.first) {
    ch->out.first = buffered;
  }
  if(ch->out.last) {
    ch->out.last->next = buffered;
  }
  ch->out.last = buffered;
  if(!ev_is_active(&src->ipc.send_retry) && !ev_is_pending(&src->ipc.send_retry)) {
    ev_timer_start(ctx->loop, &src->ipc.send_retry);
  }
  return true;
}

bool shuso_ipc_send(shuso_t *ctx, shuso_process_t *dst, const uint8_t code, void *ptr) {
  shuso_process_t *src = ctx->process;
  if(src->ipc.out.first) {
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
    .receive = receive ? receive : (shuso_ipc_receive_fn *)&do_nothing,
    .cancel = cancel ? cancel : (shuso_ipc_cancel_fn *)&do_nothing
  };
  return true;
}

static void ipc_send_retry_cb(EV_P_ ev_timer *w, int revents) {
  //TODO
}

static void ipc_receive_retry_cb(EV_P_ ev_timer *w, int revents) {
  //TODO
}

static void ipc_receive_cb(EV_P_ ev_async *w, int revents) {
  shuso_t            *ctx = ev_userdata(loop);
  shuso_process_t    *proc = w->data;
  shuso_ipc_inbuf_t  *in = &proc->ipc.in;
  size_t              last_reserve = in->last_reserve, last_release = in->last_release;
  uint_fast8_t        code;
  intptr_t            ptr;
  for(size_t i=in->first; i<=last_release; i++) {
    code = in->code[i];
    if(!code) {
      //this ipc alert is not ready yet. it will be ready really soon though. retry quite rather very soon
      if(!ev_is_active(&proc->ipc.receive_retry) && !ev_is_pending(&proc->ipc.receive_retry)) {
        ev_timer_start(ctx->loop, &proc->ipc.receive_retry);
      }
      return;
    }
    ptr = in->ptr[i];
    ctx->common->ipc_handlers[code].receive(ctx, code, (void *)ptr);
    in->code[i]=0;
    in->first++;
  }
}
