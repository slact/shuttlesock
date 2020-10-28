#ifndef SHUTTLESOCK_IO_LIBURING_H
#include <shuttlesock/common.h>


#define IORING_CORO_BEGIN(io) \
shuso_io_t *__ioring_coro_io = io; \
switch(__ioring_coro_io->) { \
  case 0:

#define IORING_CORO_YIELD(fn) \
    *___handler_stage = __LINE__; \
    fn; \
    return; \
  case __LINE__:

#define IORING_CORO_END(stageptr) \
} \
assert(stageptr == ___handler_stage); \
*___handler_stage = 0

#endif
