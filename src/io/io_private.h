#include <shuttlesock/common.h>

bool shuso_io_iovec_op_update_and_check_completion(struct iovec **iov_ptr, size_t *iovcnt_ptr, ssize_t written);
bool shuso_io_op_update_and_check_completion(shuso_io_t *io, ssize_t result_sz);

socklen_t shuso_io_af_sockaddrlen(sa_family_t fam);
void shuso_io_update_fd_closed_status_from_op_result(shuso_io_t *io, shuso_io_opcode_t op, int result);

void shuso_io_op_cleanup(shuso_io_t *io);

void shuso_io_run_handler(shuso_io_t *io);
