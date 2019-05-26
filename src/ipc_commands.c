#include <shuttlesock.h>
#include <shuttlesock/ipc.h>
#include "shuttlesock_private.h"
#include <stdlib.h>

static void test_handle(shuso_t *ctx, const uint8_t code, void *ptr) {
  
}
static void test_cancel(shuso_t *ctx, const uint8_t code, void *ptr) {
  
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
  if(!shuso_ipc_add_handler(ctx, "test", SHUTTLESOCK_IPC_CMD_TEST, test_handle, test_cancel)) {
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
