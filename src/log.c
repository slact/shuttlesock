#include <shuttlesock/log.h>
#include <shuttlesock.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>

#define NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))

static NONNULL(1,3) void shuso_log_level_vararg(shuso_t *S, shuso_loglevel_t level, const char *fmt, va_list args)   {
  char *log = S->logbuf;
  char *cur = log;
  int procnum = S->procnum;
  const char *procname;
  const char *lvl;
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
    procname = "worker ";
  }
  else {
    procname = "??????   ";
  }
  switch(level) {
    case SHUSO_LOG_FATAL:
      lvl = "FATAL";
      break;
    case SHUSO_LOG_CRITICAL:
      lvl = "CRITICAL";
      break;
    case SHUSO_LOG_ERROR:
      lvl = "ERROR";
      break;
    case SHUSO_LOG_WARNING:
      lvl = "WARNING";
      break;
    case SHUSO_LOG_NOTICE:
      lvl = "NOTICE";
      break;
    case SHUSO_LOG_INFO:
      lvl = "INFO";
      break;
    case SHUSO_LOG_DEBUG:
      lvl = "DEBUG";
      break;
    default:
      lvl = "???";
      raise(SIGABRT);
      break;
  }
  
  if(procnum >= SHUTTLESOCK_WORKER) {
    cur += sprintf(cur, "[%d %s %i] %s: ", getpid(), procname, procnum, lvl);
  }
  else {
    cur += sprintf(cur, "[%d %s] %s: ", getpid(), procname, lvl);
  }
  
  cur += vsprintf(cur, fmt, args);

  cur += sprintf(cur, "\n");
  write((S)->common->log.fd, log, cur - log);
}

void shuso_log(shuso_t *S, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  shuso_log_level_vararg(S, SHUTTLESOCK_DEFAULT_LOGLEVEL, fmt, args);
  va_end (args);
}

void shuso_log_fatal(shuso_t *S, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  shuso_log_level_vararg(S, SHUSO_LOG_FATAL, fmt, args);
  va_end (args);
}

void shuso_log_critical(shuso_t *S, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  shuso_log_level_vararg(S, SHUSO_LOG_CRITICAL, fmt, args);
  va_end (args);
}

void shuso_log_error(shuso_t *S, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  shuso_log_level_vararg(S, SHUSO_LOG_ERROR, fmt, args);
  va_end (args);
}

void shuso_log_warning(shuso_t *S, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  shuso_log_level_vararg(S, SHUSO_LOG_WARNING, fmt, args);
  va_end (args);
}

void shuso_log_notice(shuso_t *S, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  shuso_log_level_vararg(S, SHUSO_LOG_NOTICE, fmt, args);
  va_end (args);
}

void shuso_log_info(shuso_t *S, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  shuso_log_level_vararg(S, SHUSO_LOG_INFO, fmt, args);
  va_end (args);
}

void shuso_log_debug(shuso_t *S, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  shuso_log_level_vararg(S, SHUSO_LOG_DEBUG, fmt, args);
  va_end (args);
}
