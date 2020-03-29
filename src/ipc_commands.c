#include <shuttlesock.h>
#include <shuttlesock/ipc.h>
#include <shuttlesock/log.h>
#include <shuttlesock/shared_slab.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>

static void signal_handle(shuso_t *S, const uint8_t code, void *ptr) {
  intptr_t sig = (intptr_t )ptr;
  if(S->procnum != SHUTTLESOCK_MASTER) {
    shuso_log_info(S, "ignore signal %ld received via IPC", sig);
    return; 
  }
  shuso_log_info(S, "master received signal %ld via IPC", sig);
  switch(sig) {
    case SIGINT:
    case SIGQUIT:
      shuso_ipc_send(S, &S->common->process.manager, SHUTTLESOCK_IPC_CMD_SHUTDOWN, NULL); 
      break;
    default:
      //ignore
      break;
  }
}

static void shutdown_handle_cb(shuso_loop *loop, shuso_ev_timer *w, int revents) {
  shuso_t           *S = shuso_state(loop, w);
  shuso_stop_t       stop_type = (shuso_stop_t )(intptr_t )(shuso_ev_data(w));
  
  shuso_remove_timer_watcher(S, w);
  
  if(S->procnum == SHUTTLESOCK_MASTER) {
    shuso_stop(S, stop_type);
  }
  else if(S->procnum == SHUTTLESOCK_MANAGER) {
    //forward it to all the workers
    shuso_stop_manager(S, stop_type);
  }
  else if(S->procnum >= SHUTTLESOCK_WORKER) {
    shuso_stop_worker(S, S->process, stop_type);
  }
}

static void shutdown_handle(shuso_t *S, const uint8_t code, void *ptr) {
  //don't want to stop from within the handler, it may free() the calling IPC coroutine before it's finished
  
  shuso_add_timer_watcher(S, 0.0, 0.0, shutdown_handle_cb, ptr);
}

static void reconfigure_handle(shuso_t *S, const uint8_t code, void *ptr) {
  
}

static void set_log_fd_handle(shuso_t *S, const uint8_t code, void *ptr) {
  S->common->log.fd = (intptr_t )ptr;
}

static void worker_started_handle(shuso_t *S, const uint8_t code, void *ptr);
static void worker_stopped_handle(shuso_t *S, const uint8_t code, void *ptr);
static void all_worker_started_handle(shuso_t *S, const uint8_t code, void *ptr);
static void manager_proxy_message_handle(shuso_t *S, const uint8_t code, void *ptr);
static void received_proxied_message_handle(shuso_t *S, const uint8_t code, void *ptr);

bool shuso_ipc_commands_init(shuso_t *S) {
  if(!shuso_ipc_add_handler(S, "signal", SHUTTLESOCK_IPC_CMD_SIGNAL, signal_handle, NULL)) {
    return false;
  }
  if(!shuso_ipc_add_handler(S, "shutdown", SHUTTLESOCK_IPC_CMD_SHUTDOWN, shutdown_handle, NULL)) {
    return false;
  }
  if(!shuso_ipc_add_handler(S, "reconfigure", SHUTTLESOCK_IPC_CMD_RECONFIGURE, reconfigure_handle, NULL)) {
    return false;
  }
  if(!shuso_ipc_add_handler(S, "set_log_fd", SHUTTLESOCK_IPC_CMD_SET_LOG_FD, set_log_fd_handle, NULL)) {
    return false;
  }
  if(!shuso_ipc_add_handler(S, "manager_proxy_message", SHUTTLESOCK_IPC_CMD_MANAGER_PROXY_MESSAGE, manager_proxy_message_handle, NULL)) {
    return false;
  }
  if(!shuso_ipc_add_handler(S, "receive_proxied_message", SHUTTLESOCK_IPC_CMD_RECEIVE_PROXIED_MESSAGE, received_proxied_message_handle, NULL)) {
    return false;
  }
  if(!shuso_ipc_add_handler(S, "worker_started", SHUTTLESOCK_IPC_CMD_WORKER_STARTED, worker_started_handle, NULL)) {
    return false;
  }
  if(!shuso_ipc_add_handler(S, "all_workers_started", SHUTTLESOCK_IPC_CMD_ALL_WORKERS_STARTED, all_worker_started_handle, NULL)) {
    return false;
  }
  if(!shuso_ipc_add_handler(S, "worker_stopped", SHUTTLESOCK_IPC_CMD_WORKER_STOPPED, worker_stopped_handle, NULL)) {
    return false;
  }

  return true;
}

static void manager_proxy_message_handle(shuso_t *S, const uint8_t code, void *ptr) {
  assert(S->procnum == SHUTTLESOCK_MANAGER);
  shuso_ipc_manager_proxy_msg_t *d = ptr; 
  if(!shuso_ipc_send(S, shuso_process(S, d->dst), SHUTTLESOCK_IPC_CMD_RECEIVE_PROXIED_MESSAGE, d)) {
    shuso_set_error(S, "failed to proxy IPC message");
    shuso_shared_slab_free(&S->common->shm, d);
  }
}

static void received_proxied_message_handle(shuso_t *S, const uint8_t code, void *ptr) {
  shuso_ipc_manager_proxy_msg_t *d = ptr;
  S->common->ipc_handlers[d->code].receive(S, d->code, d->pd);
}


static void worker_started_handle(shuso_t *S, const uint8_t code, void *ptr) {
  assert(S->procnum == SHUTTLESOCK_MANAGER);
  //int worker_procnum = (intptr_t )ptr;
  bool all_workers_running = true;
  SHUSO_EACH_WORKER(S, worker) {
    all_workers_running = all_workers_running && *worker->state == SHUSO_STATE_RUNNING;
  }
  if(all_workers_running) {
    if(!S->common->process.all_workers_running) {
      shuso_ipc_send(S, &S->common->process.master, SHUTTLESOCK_IPC_CMD_ALL_WORKERS_STARTED, NULL); 
      shuso_core_module_event_publish(S, "manager.workers_started", SHUSO_OK, NULL);
      SHUSO_EACH_WORKER(S, worker) {
        shuso_ipc_send(S, worker, SHUTTLESOCK_IPC_CMD_ALL_WORKERS_STARTED, NULL); 
      }
    }
    S->common->process.all_workers_running = true;
  }
}

static void all_worker_started_handle(shuso_t *S, const uint8_t code, void *ptr) {
  const char *evname;
  if(S->procnum == SHUTTLESOCK_MASTER) {
    evname = "master.workers_started";
  }
  else if(S->procnum == SHUTTLESOCK_MANAGER) {
    evname = "manager.workers_started";
  }
  else if(S->procnum >= SHUTTLESOCK_WORKER) {
    evname = "worker.workers_started";
  }
  else {
    shuso_log_error(S, "received ALL_WORKERS_STARTED IPC command in unexpected procnum %i", S->procnum);
    return;
  }
  shuso_core_module_event_publish(S, evname, SHUSO_OK, NULL);
}

static void worker_stopped_handle(shuso_t *S, const uint8_t code, void *ptr) {
  assert(S->procnum == SHUTTLESOCK_MANAGER);
  
}
