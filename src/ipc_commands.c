#include <shuttlesock.h>
#include <shuttlesock/ipc.h>
#include <shuttlesock/log.h>
#include "shuttlesock_private.h"
#include <stdlib.h>

static void signal_handle(shuso_t *ctx, const uint8_t code, void *ptr) {
  intptr_t sig = (intptr_t )ptr;
  shuso_log(ctx, "got signal %ld via IPC", sig);
}
static void signal_cancel(shuso_t *ctx, const uint8_t code, void *ptr) {
  
}

static void shutdown_handle(shuso_t *ctx, const uint8_t code, void *ptr) {
  
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
