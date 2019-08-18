#include <shuttlesock/log.h>
#include <shuttlesock.h>
#include <unistd.h>
#include <stdio.h>

void shuso_log(shuso_t *ctx, const char *fmt, ...) {
  char *log = ctx->logbuf;
  char *cur = log;
  int procnum = ctx ? ctx->procnum : SHUTTLESOCK_UNKNOWN_PROCESS;
  char *procname;
  if(procnum == SHUTTLESOCK_NOPROCESS) {
    procname = "none     ";
  }
  else if(procnum == SHUTTLESOCK_MASTER) {
    procname = "master   ";
  }
  else if(procnum == SHUTTLESOCK_MANAGER) {
    procname = "manager  ";
  }
  else if(procnum > SHUTTLESOCK_MANAGER) {
    procname = "worker   ";
  }
  else {
    procname = "??????   ";
  }
  
  if(procnum >= SHUTTLESOCK_WORKER) {
    cur += sprintf(cur, "[%d %s %i]", getpid(), procname, procnum);
  }
  else {
    cur += sprintf(cur, "[%d %s] ", getpid(), procname);
  }
  
  va_list args;
  va_start(args, fmt);
  cur += vsprintf(cur, fmt, args);
  va_end (args);

  cur += sprintf(cur, "\n");
  write((ctx)->common->log.fd, log, cur - log);
}
