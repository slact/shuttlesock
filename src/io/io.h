#include <shuttlesock/common.h>

bool shuso_io_iovec_op_update_and_check_completion(struct iovec **iov_ptr, size_t *iovcnt_ptr, ssize_t written);
bool shuso_io_op_update_and_check_completion(shuso_io_t *io, ssize_t result_sz);
socklen_t shuso_io_af_sockaddrlen(sa_family_t fam);

void shuso_io_run_handler(shuso_io_t *io);
