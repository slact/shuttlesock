#include <shuttlesock.h>
#include <shuttlesock/ipc.h>
#include <shuttlesock/log.h>
#include "shuttlesock_private.h"
#include <stdlib.h>
#include <signal.h>

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
  if(ctx->procnum == SHUTTLESOCK_MASTER) {
    //do nothing i guess?
  }
  else if(ctx->procnum == SHUTTLESOCK_MANAGER) {
    //forward it to all the workers
    shuso_ipc_send_workers(ctx, SHUTTLESOCK_IPC_CMD_SHUTDOWN, ptr); 
  }
}
static void shutdown_cancel(shuso_t *ctx, const uint8_t code, void *ptr) {
  
}

static void reconfigure_handle(shuso_t *ctx, const uint8_t code, void *ptr) {
  
}
static void reconfigure_cancel(shuso_t *ctx, const uint8_t code, void *ptr) {
  
}

static void reconfigure_response_handle(shuso_t *ctx, const uint8_t code, void *ptr) {
  
}
static void reconfigure_response_cancel(shuso_t *ctx, const uint8_t code, void *ptr) {
  
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
  if(!shuso_ipc_add_handler(ctx, "reconfigure_response", SHUTTLESOCK_IPC_CMD_RECONFIGURE_RESPONSE,
                        reconfigure_response_handle, reconfigure_response_cancel)) {
    return false;
  }
  return true;
}
