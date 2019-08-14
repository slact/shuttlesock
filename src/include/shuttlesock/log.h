#ifndef SHUTTLESOCK_LOG_H
#define SHUTTLESOCK_LOG_H

#include <shuttlesock.h>
#include <unistd.h>
#include <stdio.h>


#define shuso_log(ctx, ...) do { \
  char ___log[512]; \
  char *___cur = ___log; \
  int __procnum = ctx ? ctx->procnum : SHUTTLESOCK_UNKNOWN_PROCESS; \
  ___cur += sprintf(___cur, "[%d %s", getpid(), (__procnum == SHUTTLESOCK_NOPROCESS ? "none     " : (__procnum == SHUTTLESOCK_MASTER ? "master   " : (__procnum == SHUTTLESOCK_MANAGER ? "manager  " : "worker ")))); \
  if(__procnum >= SHUTTLESOCK_WORKER) { \
    ___cur += sprintf(___cur, "%i ", __procnum); \
  } \
  ___cur += sprintf(___cur, "] "); \
  ___cur += sprintf(___cur, __VA_ARGS__); \
  ___cur += sprintf(___cur, "\n"); \
  write((ctx)->common->log.fd, ___log, ___cur - ___log); \
} while(0)

#endif //SHUTTLESOCK_LOG_H
