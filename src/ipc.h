#include <shuttlesock.h>

typedef struct {
  uint32_t  size;
  uint32_t  code;
  char      data[];
} ipc_packet_t;

bool ipc_init_pipe(shuso_t *ctx, shuso_process_t *process);
bool ipc_add_pipe_reader(shuso_t *ctx, shuso_process_t *process);
bool ipc_add_pipe_writer(shuso_t *ctx, shuso_process_t *process);
