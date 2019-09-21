#ifndef __SHUTTLESOCK_TEST_H
#define __SHUTTLESOCK_TEST_H

#include <shuttlesock.h>
#include <shuttlesock/log.h>
#include <shuttlesock/shared_slab.h>
#include <shuttlesock/sysutil.h>
#include <stdio.h>
#include <stdatomic.h>
#define SNOW_ENABLED 1
#include "snow.h"
#include <signal.h>
#include <sys/mman.h>

#include <arpa/inet.h>
#include <netdb.h>

typedef struct {
  bool    verbose;
} test_config_t;

test_config_t test_config;

#undef snow_main_decls
#define snow_main_decls \
        void snow_break() {} \
        void snow_rerun_failed() {raise(SIGSTOP);} \
        struct _snow _snow; \
        int _snow_inited = 0

#include <stdatomic.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <shuttlesock/sysutil.h>

typedef struct {
  _Atomic pid_t    pid;
  _Atomic int      status;
} test_child_result_t;

test_child_result_t *child_result;

typedef struct {
  _Atomic pid_t    master_pid;
  _Atomic pid_t    manager_pid;
  struct {       //count
    _Atomic int      start_master;
    _Atomic int      stop_master;
    _Atomic int      start_manager;
    _Atomic int      stop_manager;
    _Atomic int      start_worker;
    _Atomic int      stop_worker;
  }                count;
} test_runcheck_t;

int dev_null;

bool strmatch(const char *str, const char *pattern);

#define shmalloc(ptr) mmap(NULL, sizeof(*ptr), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED,-1, 0)
#define shmfree(ptr) munmap(ptr, sizeof(*ptr))

#define shmalloc_sz(ptr, sz) mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED,-1, 0)
#define shmfree_sz(ptr, sz) munmap(ptr, sz)

#define assert_shuso_error(S, errmsg) \
  do { \
    if(!shuso_last_error(S)) { \
      snow_fail("expected to have error matching \"%s\", but there was no error", errmsg); \
    } \
    if(!strmatch(shuso_last_error(S), errmsg)) { \
      snow_fail("shuttlesock error \"%s\" didn't match \"%s\"", shuso_last_error(S), errmsg); \
    } \
  } while(0)


#define assert_shuso(S) \
do { \
  snow_fail_update(); \
  if(shuso_is_forked_manager(S)) { \
    snow_bail(); \
  } \
  else { \
    test_runcheck_t *___chk = S->common->phase_handlers.privdata; \
    if(S->common->process.master.pid != ___chk->master_pid) { \
      snow_fail("expected S->common->process.master.pid to be %d, got %d", ___chk->master_pid, S->common->process.master.pid); \
    } \
    if(S->common->process.manager.pid != ___chk->manager_pid) { \
      snow_fail("expected S->common->process.manager.pid to be %d, got %d", ___chk->manager_pid, S->common->process.manager.pid); \
    } \
    if(___chk->count.start_master != 1) { \
      snow_fail("expected count.start_master to be 1, got %d", ___chk->count.start_master); \
    } \
    if(___chk->count.stop_master != 1) { \
      snow_fail("expected count.stop_master to be 1, got %d", ___chk->count.stop_master); \
    } \
    if(___chk->count.start_manager != 1) { \
      snow_fail("expected count.start_manager to be 1, got %d", ___chk->count.start_manager); \
    } \
    if(___chk->count.stop_manager != 1) { \
      snow_fail("expected count.stop_master to be 1, got %d", ___chk->count.stop_manager); \
    } \
    if(S->common->config.workers != 0) { \
      asserteq(S->common->config.workers, ___chk->count.start_worker, "wrong worker start count"); \
      asserteq(S->common->config.workers, ___chk->count.stop_worker, "wrong worker stop count"); \
    } \
    else { \
      asserteq(shuso_system_cores_online(), ___chk->count.start_worker, "wrong worker start count"); \
      asserteq(shuso_system_cores_online(), ___chk->count.stop_worker, "wrong worker stop count"); \
    }\
  } \
} while(0)

#define assert_shuttlesock_ok(S) \
do { \
  int ___status = 0, ___exit_status; \
  if(shuso_is_forked_manager(S)) { \
    snow_bail(); \
  } \
  else { \
    if(waitpid(S->common->process.manager.pid, &___status, 0) == -1) { \
      if(errno == ECHILD) { \
        asserteq(S->common->process.manager.pid, child_result->pid, "weird child pid found, don't know what to make of it"); \
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

shuso_t *___runcheck_shuso_create(void);
#define runcheck_shuso_create(...) \
  ((_snow.filename = __FILE__, _snow.linenum = __LINE__,  ___runcheck_shuso_create(__VA_ARGS__)))
  
void stop_timer(EV_P_ shuso_ev_timer *, int);

#define skip(...) while(0)

typedef struct {
  size_t largesz;
  off_t  count;
  off_t  large;
  size_t used;
  int    stack_count;
  shuso_stalloc_frame_t stack[SHUTTLESOCK_STALLOC_STACK_SIZE];
} test_stalloc_stats_t;

void fill_stalloc(shuso_stalloc_t *st, test_stalloc_stats_t *stats, size_t minsz, size_t maxsz, int large_alloc_interval, int total_items, int stack_push_count);

bool allocd_ptr_value_correct(char *ptr, size_t sz);

#endif //__SHUTTLESOCK_TEST_H
