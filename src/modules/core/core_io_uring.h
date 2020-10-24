#ifndef SHUTTLESOCK_CORE_IO_URING
#include <shuttlesock/common.h>

#define SHUTTLESOCK_CORE_IO_URING_MASTER_ENTRIES 16
#define SHUTTLESOCK_CORE_IO_URING_MANAGER_ENTRIES 64

#define SHUTTLESOCK_CORE_IO_URING_CQE_BATCH_SIZE 32

bool shuso_core_io_uring_setup(shuso_t *S);
bool shuso_core_io_uring_teardown(shuso_t *S);

#endif
