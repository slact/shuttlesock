#include <shuttlesock.h>
#include <unistd.h>
#include <stdio.h>

#define shuso_log(ctx, ...) do { \
  printf("[%d %s] ", getpid(), (ctx->procnum == SHUTTLESOCK_NOPROCESS ? "   none" : (ctx->procnum == SHUTTLESOCK_MASTER ? " master" : (ctx->procnum == SHUTTLESOCK_MANAGER ? "manager" : " worker")))); \
  printf(__VA_ARGS__); \
  printf("\n"); \
} while(0)

#define shuso_loop_log(loop, ...) \
  shuso_log(((shuso_t *)ev_userdata(loop)), __VA_ARGS__)
  

