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

static void signal_handle(shuso_t *ctx, const uint8_t code, void *ptr) {
  intptr_t sig = (intptr_t )ptr;
  if(ctx->procnum != SHUTTLESOCK_MASTER) {
    shuso_log_info(ctx, "ignore signal %ld received via IPC", sig);
    return; 
  }
  shuso_log_info(ctx, "master received signal %ld via IPC", sig);
  switch(sig) {
    case SIGINT:
    case SIGQUIT:
      shuso_ipc_send(ctx, &ctx->common->process.manager, SHUTTLESOCK_IPC_CMD_SHUTDOWN, NULL); 
      break;
    default:
      //ignore
      break;
  }
}


static void shutdown_handle(shuso_t *ctx, const uint8_t code, void *ptr) {
  shuso_stop_t stop_type = (shuso_stop_t )(intptr_t )ptr;
  if(ctx->procnum == SHUTTLESOCK_MASTER) {
    shuso_stop(ctx, stop_type);
  }
  else if(ctx->procnum == SHUTTLESOCK_MANAGER) {
    //forward it to all the workers
    shuso_stop_manager(ctx, stop_type);
  }
  else if(ctx->procnum >= SHUTTLESOCK_WORKER) {
    shuso_stop_worker(ctx, ctx->process, stop_type);
  }
}

static void reconfigure_handle(shuso_t *ctx, const uint8_t code, void *ptr) {
  
}

static void set_log_fd_handle(shuso_t *ctx, const uint8_t code, void *ptr) {
  ctx->common->log.fd = (intptr_t )ptr;
}

static void open_listener_sockets_handle(shuso_t *ctx, const uint8_t code, void *ptr);
static void open_listener_sockets_response_handle(shuso_t *ctx, const uint8_t code, void *ptr);

bool shuso_ipc_commands_init(shuso_t *ctx) {
  if(!shuso_ipc_add_handler(ctx, "signal", SHUTTLESOCK_IPC_CMD_SIGNAL, signal_handle, NULL)) {
    return false;
  }
  if(!shuso_ipc_add_handler(ctx, "shutdown", SHUTTLESOCK_IPC_CMD_SHUTDOWN, shutdown_handle, NULL)) {
    return false;
  }
  if(!shuso_ipc_add_handler(ctx, "reconfigure", SHUTTLESOCK_IPC_CMD_RECONFIGURE, reconfigure_handle, NULL)) {
    return false;
  }
  if(!shuso_ipc_add_handler(ctx, "set_log_fd", SHUTTLESOCK_IPC_CMD_SET_LOG_FD, set_log_fd_handle, NULL)) {
    return false;
  }
  if(!shuso_ipc_add_handler(ctx, "open_listener_sockets", SHUTTLESOCK_IPC_CMD_OPEN_LISTENER_SOCKETS, open_listener_sockets_handle, NULL)) {
    return false;
  }
  if(!shuso_ipc_add_handler(ctx, "open_listener_sockets_response", SHUTTLESOCK_IPC_CMD_OPEN_LISTENER_SOCKETS_RESPONSE, open_listener_sockets_response_handle, NULL)) {
    return false;
  }

  return true;
}


static bool command_open_listener_sockets_from_manager(shuso_t *ctx, shuso_hostinfo_t *hostinfo, int count, shuso_sockopts_t *sockopts, shuso_ipc_open_sockets_fn callback, void *pd);
static bool command_open_listener_sockets_from_worker(shuso_t *ctx, shuso_hostinfo_t *hostinfo, int count, shuso_sockopts_t *sockopts, shuso_ipc_open_sockets_fn callback, void *pd);

bool shuso_ipc_command_open_listener_sockets(shuso_t *ctx, shuso_hostinfo_t *hostinfo, int count, shuso_sockopts_t *sockopts, shuso_ipc_open_sockets_fn callback, void *pd) {
  
  int                  *opened = NULL;
  int                   i = 0;
  int                   socktype;
  const char           *err = NULL;
  size_t                path_len = 0;
  struct sockaddr      *sa = NULL;
  struct sockaddr_un    sa_unix;
  struct sockaddr_in    sa_inet;
  struct sockaddr_in6   sa_inet6;
  size_t                addr_len = 0;
  assert(count > 0);
  //shuso_log_debug(ctx, "opening listener sockets...");
  if(ctx->procnum == SHUTTLESOCK_MASTER) {
    if((opened = calloc(count, sizeof(*opened))) == NULL) {
      err = "failed to allocate memory for opening listening sockets";
      goto fail;
    }
    if(hostinfo->addr_family == AF_UNIX) {
      socktype = SOCK_STREAM;
      memset(&sa_unix, 0, sizeof(sa_unix));
      sa_unix.sun_family = AF_UNIX;
      path_len = strlen(hostinfo->path);
      if((path_len+1) > sizeof(sa_unix.sun_path)) {
        err = "unix socket path is too long";
        goto fail;
      }
      memcpy(&sa_unix.sun_path, hostinfo->path, path_len);
      sa = (struct sockaddr *)&sa_unix;
      addr_len = sizeof(sa_unix);
    }
    else {
      if(hostinfo->udp) {
        socktype = SOCK_DGRAM;
      }
      else {
        socktype = SOCK_STREAM;
      }
      if(hostinfo->addr_family == AF_INET) {
        memset(&sa_inet, 0, sizeof(sa_inet));
        sa_inet.sin_family = AF_INET;
        sa_inet.sin_port = htons(hostinfo->port);
        sa_inet.sin_addr = hostinfo->addr;
        sa = (struct sockaddr *)&sa_inet;
        addr_len = sizeof(sa_inet);
      }
      else if(hostinfo->addr_family == AF_INET6) {
        memset(&sa_inet6, 0, sizeof(sa_inet6));
        sa_inet6.sin6_family = AF_INET6;
        sa_inet6.sin6_port = htons(hostinfo->port);
        //TODO: flowinfo?
        sa_inet6.sin6_addr = hostinfo->addr6;
        //TODO: scope_id?
        sa = (struct sockaddr *)&sa_inet6;
        addr_len = sizeof(sa_inet6);
      }
    }
    assert(sa);
    for(i=0; i < count; i++) {
      //shuso_log_debug(ctx, "open socket #%d", i);
      opened[i]=socket(hostinfo->addr_family, socktype, 0);
      if(opened[i] == -1) {
        err = "failed to create listener socket";
        goto fail;
      }
      for(unsigned j=0; j < sockopts->count; j++) {
        //shuso_log_debug(ctx, "setsockopt #%d opt #%d", i, j);
        shuso_sockopt_t *opt = &sockopts->array[j];
        if(setsockopt(opened[i], opt->level, opt->name, &opt->intvalue, sizeof(opt->intvalue)) < 0) {
          err = "failed to set sockopt on new listener socket";
          goto fail;
        }
      }
      
      shuso_set_nonblocking(opened[i]);
      //shuso_log_debug(ctx, "bind #%d", i);
      if(bind(opened[i], sa, addr_len) == -1) {
        err = "failed to bind listener socket";
        goto fail;
      }
    }
    callback(ctx, SHUSO_OK, hostinfo, opened, count, pd);
    free(opened);
    return true;
  }
  else if(ctx->procnum == SHUTTLESOCK_MANAGER) {
    return command_open_listener_sockets_from_manager(ctx, hostinfo, count, sockopts, callback, pd);
  }
  else if(ctx->procnum >= SHUTTLESOCK_WORKER) {
    return command_open_listener_sockets_from_worker(ctx, hostinfo, count, sockopts, callback, pd);
  }
  
fail:
  if(err) {
    shuso_set_error(ctx, err);
  }
  callback(ctx, SHUSO_FAIL, hostinfo, NULL, 0, pd);
  if(opened) {
    for(int j=0; j<count; j++) {
      if (opened[i]!=-1) {
        close(opened[i]);
      }
    }
    free(opened);
  }
  return false; 
}

typedef struct {
  shuso_hostinfo_t     hostinfo;
  char                *path;
  shuso_ipc_open_sockets_fn *callback;
  void                *pd;
  shuso_status_t       status;
  char                 err[512];
  shuso_sockopts_t     sockopts;
  unsigned             socket_count;
  int                  sockets[];
} ipc_open_sockets_t;


static bool free_shared_open_sockets_struct(shuso_t *ctx, ipc_open_sockets_t *sockreq) {
  if(sockreq->path) {
    shuso_shared_slab_free(&ctx->common->shm, sockreq->path);
  }
  if(sockreq->hostinfo.name) {
    shuso_shared_slab_free(&ctx->common->shm, (void *)sockreq->hostinfo.name);
  }
  if(sockreq->sockopts.array) {
    shuso_shared_slab_free(&ctx->common->shm, (void *)sockreq->sockopts.array);
  }
  shuso_shared_slab_free(&ctx->common->shm, sockreq);
  return true;
}

static bool command_open_listener_sockets_from_manager(shuso_t *ctx, shuso_hostinfo_t *hostinfo, int count, shuso_sockopts_t *sockopts, shuso_ipc_open_sockets_fn callback, void *pd) {
  assert(ctx->procnum == SHUTTLESOCK_MANAGER);
  assert(count > 0);
  const char          *err = NULL;
  ipc_open_sockets_t  *sockreq = shuso_shared_slab_alloc(&ctx->common->shm, sizeof(*sockreq) + sizeof(int)*count);
  if(!sockreq) {
    err = "failed to allocate shared memory for listener socket request";
    goto fail;
  }
  
  sockreq->sockopts.array = NULL;
  if(sockopts) {
    sockreq->sockopts.array = shuso_shared_slab_alloc(&ctx->common->shm, sizeof(shuso_sockopt_t)*sockopts->count);
    if(!sockreq->sockopts.array) {
      err = "failed to allocate shared memory for listener socket sockopts";
      goto fail;
    }
    sockreq->sockopts.count = sockopts->count;
    memcpy(sockreq->sockopts.array, sockopts->array, sizeof(shuso_sockopt_t)*sockopts->count);
  }
  
  sockreq->hostinfo = *hostinfo;
  if(hostinfo->name && strlen(hostinfo->name) > 0) {
    sockreq->hostinfo.name =  shuso_shared_slab_alloc(&ctx->common->shm, strlen(hostinfo->name)+1);
    if(!sockreq->hostinfo.name) {
      err = "failed to allocate shared memory for listener socket request name";
      goto fail;
    }
    strcpy((char *)sockreq->hostinfo.name, hostinfo->name);
  }
  
  if(hostinfo->addr_family == AF_UNIX) {
    sockreq->path = shuso_shared_slab_alloc(&ctx->common->shm, strlen(hostinfo->path)+1);
    if(!sockreq->path) {
      err = "failed to allocate shared memory for listener socket request path";
      goto fail;
    }
    strcpy(sockreq->path, hostinfo->path);
    sockreq->hostinfo.path = sockreq->path;
  }
  
  sockreq->socket_count = count;
  sockreq->callback = callback;
  sockreq->pd = pd;
  
  if(!shuso_ipc_send(ctx, &ctx->common->process.master, SHUTTLESOCK_IPC_CMD_OPEN_LISTENER_SOCKETS, sockreq)) {
    err = "failed to send ipc request to master to open listerener sockets";
    goto fail;
  }
  return true;
fail:
  if(err) {
    shuso_set_error(ctx, err);
  }
  if(sockreq) free_shared_open_sockets_struct(ctx, sockreq);
  callback(ctx, SHUSO_FAIL, hostinfo, NULL, 0, pd);
  return false;
}

static void open_listener_sockets_handle_callback(shuso_t *ctx, shuso_status_t status, shuso_hostinfo_t *hostinfo, int *sockets, int socket_count, void *pd) {
  ipc_open_sockets_t *sockreq = pd;
  sockreq->status = status;
  shuso_ipc_send(ctx, &ctx->common->process.manager, SHUTTLESOCK_IPC_CMD_OPEN_LISTENER_SOCKETS_RESPONSE, sockreq);
  if(status == SHUSO_OK) {
    sockreq->status = SHUSO_OK;
    for(int i=0; i<socket_count; i++) {
      if(!shuso_ipc_send_fd(ctx, &ctx->common->process.manager, sockets[i], (uintptr_t)sockreq, (void *)(intptr_t)i)) {
        //TODO: send ABORT to socket receiver
        shuso_log_error(ctx, "shuso_ipc_send_fd #%d socket %d FAILED: %d %s", i, sockets[i], errno, strerror(errno));
      }
      close(sockets[i]);
      sockets[i] = -1;
    }
  }
  else {
    shuso_log_error(ctx, "SHUTTLESOCK_IPC_CMD_OPEN_LISTENER_SOCKETS_RESPONSE failed");
  }
}
static void open_listener_sockets_handle(shuso_t *ctx, const uint8_t code, void *ptr) {
  ipc_open_sockets_t *sockreq = ptr;
  assert(ctx->procnum == SHUTTLESOCK_MASTER);
  shuso_ipc_command_open_listener_sockets(ctx, &sockreq->hostinfo, sockreq->socket_count, &sockreq->sockopts, open_listener_sockets_handle_callback, sockreq);
}

static void listener_socket_receiver(shuso_t *ctx, bool ok, uintptr_t ref, int fd, void *received_pd, void *pd){
  uintptr_t n = (uintptr_t)received_pd;
  ipc_open_sockets_t *sockreq = pd;
  if(ok) {
    //shuso_log_debug(ctx, "listener_socket_receiver ok...");
    assert(n < sockreq->socket_count);
    assert(fd != -1);
    sockreq->sockets[n]=fd;
    //shuso_log_debug(ctx, "RECEIVED SOCKET #%lu %d", n, fd);
    if(n+1 == sockreq->socket_count) { //ok, that was the last one
      //shuso_log_debug(ctx, "received socket, ok that was the last one");
      shuso_ipc_receive_fd_finish(ctx, ref);
      sockreq->callback(ctx, true, &sockreq->hostinfo, sockreq->sockets, sockreq->socket_count, sockreq->pd);
      free_shared_open_sockets_struct(ctx, sockreq);
    }
  }
  else {
    shuso_log_error(ctx, "listener_socket_receiver for ref %p failed", (void *)ref);
    shuso_ipc_receive_fd_finish(ctx, ref);
    sockreq->callback(ctx, false, &sockreq->hostinfo, NULL, 0, sockreq->pd);
    free_shared_open_sockets_struct(ctx, sockreq);
  }
}

static void open_listener_sockets_response_handle(shuso_t *ctx, const uint8_t code, void *ptr) {
  //shuso_log_debug(ctx, "open_listener_sockets_response_handle");
  shuso_ipc_receive_fd_start(ctx, "open listener sockets response", 1.0, listener_socket_receiver, (uintptr_t) ptr, ptr);
}


static bool command_open_listener_sockets_from_worker(shuso_t *ctx, shuso_hostinfo_t *hostinfo, int count, shuso_sockopts_t *sockopts, shuso_ipc_open_sockets_fn callback, void *pd) {
  
  return true;
}
