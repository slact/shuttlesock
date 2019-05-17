#include <stdlib.h>
#include <shuttlesock.h>
#include <ev.h>
#include <assert.h>
#include "shuttlesock_private.h"

static bool shuso_spawn_manager(shuso_t *ctx);
static bool shuso_worker_spawn(shuso_t *ctx);

static void cleanup_master_loop(EV_P_ ev_cleanup *w, int revents);

shuso_t *shuso_create(unsigned int ev_loop_flags, shuso_handlers_t *handlers, const char **err) {
  shuso_shared_t     *shared_ctx;
  shuso_t            *ctx;
  struct ev_loop     *loop;
  
  if((shared_ctx = calloc(1, sizeof(*shared_ctx))) == NULL) {
    if(err) *err = "not enough memory to allocate shared_ctx";
    return NULL;
  }
  if((ctx = calloc(1, sizeof(*ctx))) == NULL) {
    free(shared_ctx);
    if(err) *err = "not enough memory to allocate ctx";
    return NULL;
  }
  
  // create the default loop so that we can catch SIGCHLD
  // http://pod.tst.eu/http://cvs.schmorp.de/libev/ev.pod#FUNCTIONS_CONTROLLING_EVENT_LOOPS:
  // "The default loop is the only loop that can handle ev_child watchers [...]"
  if((loop = ev_default_loop(ev_loop_flags)) == NULL) {
    free(ctx);
    free(shared_ctx);
    if(err) *err = "failed to create event loop";
    return NULL;
  }
  
  if(handlers) {
    shared_ctx->handlers = *handlers;
  }
  *ctx = (shuso_t ){
    .procnum = SHUTTLESOCK_NOPROCESS,
    .loop    = loop,
    .shared  = shared_ctx
  };
  
  ev_set_userdata(loop, ctx);
  ev_cleanup_init(&ctx->loop_cleanup, cleanup_master_loop);
  return ctx;
}

bool shuso_destroy(shuso_t *ctx) {
  assert(ctx->loop == NULL);
  return set_error(ctx, "loop is still present. stop it first.");
}

static bool shuso_spawn_manager(shuso_t *ctx) {
  return true;
}
static bool shuso_spawn_worker(shuso_t *ctx) {
  return true;
}

bool shuso_run(shuso_t *ctx) {
  //attach master signal handlers
  return true;
}

bool set_error(shuso_t *ctx, const char *err) {
  ctx->errmsg = err;
  return false;
}

#define DELETE_BASE_WATCHERS(ctx, watcher_type) \
  for(ev_##watcher_type##_link_t *cur = ctx->base_watchers.watcher_type.head, *next = NULL; cur != NULL; cur = next) { \
    next = cur->next; \
    ev_##watcher_type##_stop(EV_A_ &cur->data); \
    free(cur); \
  } \
  llist_init(ctx->base_watchers.watcher_type)

static void cleanup_master_loop(EV_P_ ev_cleanup *w, int revents) {
  shuso_t *ctx = ev_userdata(EV_A);
  
  DELETE_BASE_WATCHERS(ctx, signal);
  DELETE_BASE_WATCHERS(ctx, child);
  DELETE_BASE_WATCHERS(ctx, io);
  DELETE_BASE_WATCHERS(ctx, timer);
  DELETE_BASE_WATCHERS(ctx, periodic);
}

#undef DELETE_BASE_WATCHERS
