#include "ipc.h"
#include <unistd.h>

bool ipc_init_pipe(shuso_t *ctx, shuso_process_t *process) {
  if(pipe((int *)(&process->ipc.pipe)) == -1) {
    return false;
  }
  shuso_set_nonblocking(process->ipc.pipe.in);
  shuso_set_nonblocking(process->ipc.pipe.out);
  return true;
}

static void pipe_reader_io_cb(EV_P_ ev_io *w, int revents) {
  //TODO
}
static void pipe_writer_io_cb(EV_P_ ev_io *w, int revents) {
  //TODO
}

bool ipc_add_pipe_reader(shuso_t *ctx, shuso_process_t *process) {
  return shuso_add_io_watcher(ctx, pipe_reader_io_cb, process, process->ipc.pipe.out, EV_READ) != NULL;
}

bool ipc_add_pipe_writer(shuso_t *ctx, shuso_process_t *process) {
  return shuso_add_io_watcher(ctx, pipe_writer_io_cb, process, process->ipc.pipe.in, EV_WRITE) != NULL;
}
