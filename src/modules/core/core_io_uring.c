#include <shuttlesock.h>
#include <shuttlesock/internal.h>
#include <shuttlesock/watchers.h>
#include "core.h"
#ifndef SHUTTLESOCK_HAVE_IO_URING
bool shuso_core_io_uring_setup(shuso_t *S) {
  S->io_uring.on = false;
  return true;
}
bool shuso_core_io_uring_teardown(shuso_t *S) {
  return true;
}
#else
#include <liburing.h>
//if we have io_uring we're in linux land so surely we have eventfd too
#include <sys/eventfd.h>


static void io_uring_eventfd_handler(shuso_loop *loop, shuso_ev_io *ev, int evflags);
static void io_uring_check_cqueue(shuso_t *S);

#define __OP(name) {.code = IORING_OP_ ## name, .str = "IORING_OP_" #name " is not supported"}
struct {
  int         code;
  const char *str;
} shuso_io_ops_needed[] = {
  __OP(READV),
  __OP(WRITEV),
  __OP(READ_FIXED),
  __OP(WRITE_FIXED),
  __OP(POLL_ADD),
  __OP(POLL_REMOVE),
  __OP(SENDMSG),
  __OP(RECVMSG),
  __OP(SEND),
  __OP(RECV),
  __OP(TIMEOUT),
  __OP(TIMEOUT_REMOVE),
  __OP(ACCEPT),
#ifdef SHUTTLESOCK_HAVE_IO_URING_OP_PROVIDE_BUFFERS
  __OP(PROVIDE_BUFFERS),
#endif
  __OP(ASYNC_CANCEL),
  __OP(LINK_TIMEOUT),
  __OP(CONNECT),
  __OP(CLOSE),
  __OP(READ),
  __OP(WRITE),
  __OP(FILES_UPDATE),
  {0, NULL}
};
#undef __OP

static bool shuso_core_io_uring_test_support(const char **err, struct io_uring *ring, struct io_uring_params *params) {
  
  if(!(params->features & IORING_FEAT_NODROP)) {
    *err = "IORING_FEAT_NODROP not supported";
    return false;
  }
  
  if(!(params->features & IORING_FEAT_SUBMIT_STABLE)) {
    *err = "IORING_FEAT_SUBMIT_STABLE not supported";
    return false;
  }
  
  struct io_uring_probe *probe = io_uring_get_probe_ring(ring);
  if(!probe) {
    *err = "capability probing not supported";
    return false;
  }
  
  for(int i=0; shuso_io_ops_needed[i].str != NULL; i++) {
    if(!io_uring_opcode_supported(probe, shuso_io_ops_needed[i].code)) {
      *err = shuso_io_ops_needed[i].str;
      free(probe);
      return false;
    }
  }
  
  free(probe);
  return true;
}

static bool uring_setup_fail(shuso_t *S, const char *fmt, ...) {
  va_list       args;
  int           enabled = S->common->config.io_uring.enabled;
  if(S->io_uring.on) {
    io_uring_queue_exit(&S->io_uring.ring);
  }
  S->io_uring.on = false;
  if(S->io_uring.eventfd != -1) {
    close(S->io_uring.eventfd);
  }
  va_start(args, fmt);
  if(enabled == SHUSO_MAYBE) {
    shuso_log_level_vararg(S, SHUSO_LOG_DEBUG, fmt, args);
  }
  else {
    shuso_set_error_vararg(S, fmt, args);
  }
  va_end(args);
  return enabled == SHUSO_MAYBE;
}

bool shuso_core_io_uring_setup(shuso_t *S) {
  int enabled;
  
  //turn off incomplete io_uring implementation for now
  enabled = false; //S->common->config.io_uring.enabled;
  
  //initial state: assi,e io_uring has not bee set up
  S->io_uring.on = false;
  S->io_uring.eventfd = -1;
  
  if(enabled == SHUSO_NO) {
    return true;
  }
  
  struct io_uring        *ring = &S->io_uring.ring;
  struct io_uring_params  params = {0};
  int                     entries;
  switch(S->procnum) {
    case SHUTTLESOCK_MASTER:
      entries = SHUTTLESOCK_CORE_IO_URING_MASTER_ENTRIES;
      break;
    case SHUTTLESOCK_MANAGER:
      entries = SHUTTLESOCK_CORE_IO_URING_MANAGER_ENTRIES;
      break;
    default:
      entries = S->common->config.io_uring.worker_entries;
      break;
  }
  
  params.flags = 0;
  if(S->common->config.io_uring.sqpoll_thread) {
    params.flags |= IORING_SETUP_SQPOLL;
    params.sq_thread_idle=S->common->config.io_uring.sqpoll_thread_idle;
  }
  
  int         rc;
  const char *err;
  if((rc = io_uring_queue_init_params(entries, ring, &params)) != 0) {
    err = strerror(-rc);
    return uring_setup_fail(S, "Failed to initialize io_uring with %d entries: %s", entries, err);
  }
  S->io_uring.on = true; //set this early so that uring_setup_fail knows to teardown the ring
  
  if(!shuso_core_io_uring_test_support(&err, ring, &params)) {
    return uring_setup_fail(S, "Failed to initialize io_uring: '%s'", err);
  }
  
  if((rc = io_uring_ring_dontfork(ring)) != 0) {
    err = strerror(-rc);
    return uring_setup_fail(S, "Failed to initialize io_uring: dontfork failed with error '%s'", err);
  }
  
  S->io_uring.eventfd = eventfd(0, EFD_NONBLOCK);
  if(S->io_uring.eventfd == -1) {
    err = strerror(rc);
    return uring_setup_fail(S, "Failed to initialize io_uring: eventfd() call failed with error '%s'", err);
  }
  
  if((rc = io_uring_register_eventfd_async(&S->io_uring.ring, S->io_uring.eventfd)) != 0) {
    err = strerror(-rc);
    return uring_setup_fail(S, "Failed to initialize io_uring: IORING_REGISTER_EVENTFD_ASYNC failed with error '%s'", err);
  }
  
  shuso_ev_init(S, &S->io_uring.watcher, S->io_uring.eventfd, EV_READ, io_uring_eventfd_handler, NULL);
  shuso_ev_io_start(S, &S->io_uring.watcher);
  shuso_log_debug(S, "io_uring started");
  return true;
}

bool shuso_core_io_uring_teardown(shuso_t *S) {
  if(shuso_ev_active(&S->io_uring.watcher)) {
    shuso_ev_stop(S, &S->io_uring.watcher);
  }
  if(S->io_uring.on) {
    io_uring_queue_exit(&S->io_uring.ring);
    shuso_log_debug(S, "io_uring stopped");
  }
  S->io_uring.on = false;
  if(S->io_uring.eventfd != -1) {
    close(S->io_uring.eventfd);
    S->io_uring.eventfd = -1;
  }
  return true;
}

static void io_uring_eventfd_handler(shuso_loop *loop, shuso_ev_io *ev, int evflags) {
  shuso_t *S = shuso_state(loop, ev);
  io_uring_check_cqueue(S);
}


static void io_uring_check_cqueue(shuso_t *S) {
  struct io_uring_cqe *cqes[SHUTTLESOCK_CORE_IO_URING_CQE_BATCH_SIZE];
  unsigned cqe_count;
  while(true) {
    cqe_count = io_uring_peek_batch_cqe(&S->io_uring.ring, cqes, SHUTTLESOCK_CORE_IO_URING_CQE_BATCH_SIZE);
    if(cqe_count == 0) {
      break;
    }
    for(unsigned i=0; i<cqe_count; i++) {
      shuso_io_uring_handle_t *handle = io_uring_cqe_get_data(cqes[i]);
      int32_t                  result = cqes[i]->res;
      uint32_t                 flags = cqes[i]->flags;
      handle->callback(S, result, flags, handle, handle->pd);
      io_uring_cqe_seen(&S->io_uring.ring, cqes[i]);
    }
  }  
}

struct io_uring_sqe *shuso_io_uring_get_sqe(shuso_t *S) {
  struct io_uring_sqe *sqe;
  return shuso_io_uring_get_sqes(S, &sqe, 1) ? sqe : NULL;
}

bool shuso_io_uring_get_sqes(shuso_t *S, struct io_uring_sqe **sqes, int num_sqes) {
  for(int i=0; i< num_sqes; i++) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&S->io_uring.ring);
    if(!sqe) {
      //maybe submit the SQE ring?
      io_uring_submit(&S->io_uring.ring);
      sqe = io_uring_get_sqe(&S->io_uring.ring);
    }
    if(!sqe) {
      //didn't get enough SQEs. recycle the ones we have by no-opping them
      for(int j=0; j<i; j++) {
        io_uring_prep_nop(sqes[j]);
      }
      io_uring_submit(&S->io_uring.ring);
      return false;
    }
    sqes[i]=sqe;
  }
  return true;
}

void shuso_io_uring_submit(shuso_t *S) {
  //TODO: batch up submissions when not using SQPOLL mode
  io_uring_submit(&S->io_uring.ring);
}

#endif
