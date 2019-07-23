#include <shuttlesock.h>
#include <shuttlesock/ipc.h>
#include <shuttlesock/log.h>
#include "shuttlesock_private.h"
#include <stdlib.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>

static void signal_handle(shuso_t *ctx, const uint8_t code, void *ptr) {
  intptr_t sig = (intptr_t )ptr;
  if(ctx->procnum != SHUTTLESOCK_MASTER) {
    shuso_log(ctx, "ignore signal %ld received via IPC", sig);
    return; 
  }
  shuso_log(ctx, "master received signal %ld via IPC", sig);
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

static void signal_cancel(shuso_t *ctx, const uint8_t code, void *ptr) {
  
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
static void shutdown_cancel(shuso_t *ctx, const uint8_t code, void *ptr) {
  
}

static void reconfigure_handle(shuso_t *ctx, const uint8_t code, void *ptr) {
  
}
static void reconfigure_cancel(shuso_t *ctx, const uint8_t code, void *ptr) {
  
}

static void set_log_fd_handle(shuso_t *ctx, const uint8_t code, void *ptr) {
  ctx->common->log.fd = (intptr_t )ptr;
}
static void set_log_fd_cancel(shuso_t *ctx, const uint8_t code, void *ptr) {
  
}

bool shuso_ipc_commands_init(shuso_t *ctx) {
  if(!shuso_ipc_add_handler(ctx, "signal", SHUTTLESOCK_IPC_CMD_SIGNAL, signal_handle, signal_cancel)) {
    return false;
  }
  if(!shuso_ipc_add_handler(ctx, "shutdown", SHUTTLESOCK_IPC_CMD_SHUTDOWN, shutdown_handle, shutdown_cancel)) {
    return false;
  }
  if(!shuso_ipc_add_handler(ctx, "reconfigure", SHUTTLESOCK_IPC_CMD_RECONFIGURE, reconfigure_handle, reconfigure_cancel)) {
    return false;
  }
  if(!shuso_ipc_add_handler(ctx, "set_log_fd", SHUTTLESOCK_IPC_CMD_SET_LOG_FD,
                        set_log_fd_handle, set_log_fd_cancel)) {
    return false;
  }

  return true;
}


bool shuso_ipc_command_open_listener_sockets(shuso_t *ctx, shuso_hostinfo_t *hostinfo, int count, void (*callback)(bool ok, shuso_t *, shuso_hostinfo_t *, int *sockets, int socket_count, void *pd), void *pd) {
  
  int                  *opened = NULL;
  int                   i = 0;
  int                   socktype;
  const char           *err = NULL;
  size_t                path_len = 0;
  struct sockaddr      *sa;
  struct sockaddr_un    sa_unix;
  struct sockaddr_in    sa_inet;
  struct sockaddr_in6   sa_inet6;
  size_t                addr_len = 0;
  
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
    
    for(i=0; i < count; i++) {
      opened[i]=socket(hostinfo->addr_family, socktype, 0);
      if(opened[i] == -1) {
        err = "failed to create listener socket";
        goto fail;
      }
      shuso_set_nonblocking(opened[i]);
      if(bind(opened[i], sa, addr_len) == -1) {
        err = "failed to bind listener socket";
        goto fail;
      }
    }
    free(opened);
  }
  
  return true;
fail:
  if(err) shuso_set_error(ctx, err);
  callback(false, ctx, hostinfo, NULL, 0, pd);
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
