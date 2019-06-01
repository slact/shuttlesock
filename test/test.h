#ifndef __SHUTTLESOCK_TEST_H
#define __SHUTTLESOCK_TEST_H

#include <stdatomic.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>

typedef struct {
  _Atomic pid_t    pid;
  _Atomic int      status;
} test_child_result_t;

test_child_result_t *child_result;

#define assert_shuttlesock_ok(ctx) \
do { \
  int ___status = 0, ___exit_status; \
  if(shuso_is_forked_manager(ctx)) { \
    snow_bail(); \
  } \
  else { \
    if(waitpid(ctx->common->process.manager.pid, &___status, 0) == -1) { \
      if(errno == ECHILD) { \
        printf("%d %d\n", ctx->common->process.manager.pid, child_result->pid); \
        asserteq(ctx->common->process.manager.pid, child_result->pid, "weird child pid found, don't know what to make of it"); \
        ___exit_status = child_result->status; \
        asserteq(0, ___exit_status, "manager exited with error"); \
      } \
      else { \
        perror("waitpid error"); \
        snow_fail_update(); \
        snow_fail("weird errno, don'tknow what to make of it"); \
      } \
    } \
    else { \
      if (WIFEXITED(___status)) { \
        ___exit_status = WEXITSTATUS(___status); \
        asserteq(0, ___exit_status, "manager exited with error"); \
      } \
      else if (WIFSIGNALED(___status)) { \
        snow_fail_update(); \
        snow_fail("weird waitpid result: killed by signal %d\n", WTERMSIG(___status)); \
      } else if (WIFSTOPPED(___status)) { \
        snow_fail_update(); \
        snow_fail("weird waitpid result: stopped by signal %d\n", WSTOPSIG(___status)); \
      } else if (WIFCONTINUED(___status)) { \
        snow_fail_update(); \
        snow_fail("weird waitpid result: continued\n"); \
      } \
    } \
  } \
} while(0)

#endif //__SHUTTLESOCK_TEST_H
