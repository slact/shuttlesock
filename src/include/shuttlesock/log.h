#ifndef SHUTTLESOCK_LOG_H
#define SHUTTLESOCK_LOG_H

#include <shuttlesock.h>
#include <unistd.h>
#include <stdio.h>


#define shuso_log(ctx, ...) do { \
  char ___log[2048]; \
  char *___cur = ___log; \
  ___cur += sprintf(___cur, "[%d %s", getpid(), (ctx->procnum == SHUTTLESOCK_NOPROCESS ? "none     " : (ctx->procnum == SHUTTLESOCK_MASTER ? "master   " : (ctx->procnum == SHUTTLESOCK_MANAGER ? "manager  " : "worker ")))); \
  if(ctx->procnum >= SHUTTLESOCK_WORKER) { \
    ___cur += sprintf(___cur, "%i ", ctx->procnum); \
  } \
  ___cur += sprintf(___cur, "] "); \
  ___cur += sprintf(___cur, __VA_ARGS__); \
  ___cur += sprintf(___cur, "\n"); \
  write((ctx)->common->log.fd, ___log, ___cur - ___log); \
} while(0)

#define shuso_loop_log(loop, ...) \
  shuso_log(((shuso_t *)ev_userdata(loop)), __VA_ARGS__)
  
#endif //SHUTTLESOCK_LOG_H
