#ifndef SHUTTLESOCK_PRIVATE_H
#define SHUTTLESOCK_PRIVATE_H

#include <shuttlesock.h>
bool set_error(shuso_t *ctx, const char *err);
shuso_process_t *procnum_to_process(shuso_t *ctx, int procnum);
int process_to_procnum(shuso_t *ctx, shuso_process_t *proc);

#endif //SHUTTLESOCK_PRIVATE_H
