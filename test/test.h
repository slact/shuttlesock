#ifndef __SHUTTLESOCK_TEST_H
#define __SHUTTLESOCK_TEST_H

#include <shuttlesock.h>
#include <stdio.h>
#include <stdatomic.h>
#define SNOW_ENABLED 1
#include "snow.h"
#include <signal.h>
#include <sys/mman.h>

#include <arpa/inet.h>
#include <netdb.h>

#include <sys/types.h>

#include <sys/wait.h>

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
  _Atomic pid_t     pid;
  _Atomic int       status;
  _Atomic uint8_t   before_started;
  _Atomic uint8_t   started;
  _Atomic uint8_t   stopped;
  _Atomic int       exit_code;
} test_process_rundata_t;

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
  struct {
    _Atomic int    master_start;
    _Atomic int    manager_start;
    _Atomic int    worker_start_before;
    _Atomic int    worker_start;
    _Atomic int    master_stop;
    _Atomic int    manager_stop;
    _Atomic int    worker_stop;
    _Atomic int    manager_workers_started;
    _Atomic int    master_workers_started;
    _Atomic int    worker_workers_started;
    _Atomic int    master_manager_exited;
    
  }                events;
  struct {
  void               (*run)(shuso_t *S, void *pd);
  void               (*verify)(shuso_t *S, void *pd);
  void                *pd;
  int                procnum;
  }                  test;
  
  double             timeout;
  bool               timeout_is_ok;
  shuso_ev_timer     timeout_timer;
  _Atomic uint8_t    timed_out;
  
  _Atomic int        workers_started;
  _Atomic int        workers_stopped;
  struct {
    test_process_rundata_t master;
    test_process_rundata_t manager;
    test_process_rundata_t worker[SHUTTLESOCK_MAX_WORKERS];
    _Atomic uint8_t        all_workers_started;
  }                process;
  
  shuso_module_t   runcheck_module;
} test_runcheck_t;

int dev_null;

shuso_t *shusoT_create(test_runcheck_t **external_ptr, double test_timeout);

#define shusoT_run_test(...) do { \
  snow_fail_update(); \
  ___shusoT_run_test(__VA_ARGS__); \
} while(0)
bool ___shusoT_run_test(shuso_t *S, int procnum, void (*run)(shuso_t *, void *), void (*verify)(shuso_t *, void *), void *pd);

bool shusoT_destroy(shuso_t *S, test_runcheck_t **chkptr);
bool strmatch(const char *str, const char *pattern);
bool ___runcheck(shuso_t *S, char **err);

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


#define assert_shuso_ok(S) \
do { \
  snow_fail_update(); \
  if(shuso_is_forked_manager(S)) { \
    snow_bail(); \
  } \
  else { \
    char *___errmsg; \
    if(!___runcheck(S, &___errmsg)) { \
      snow_fail("%s", ___errmsg); \
    } \
  } \
} while(0)

#pragma GCC system_header
#pragma GCC diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"

#define assert_util(x, expl...) \
do { \
  const char *explanation = "" expl; \
  if (!(x)) \
      _snow_fail_expl(explanation, "Assertion failed: %s", #x); \
} while (0)


#define skip(...) while(0)

#define assert_luaL_dostring(L, str) do { \
  if(luaL_loadstring(L, (str)) != LUA_OK) { \
    snow_fail("%s", lua_tostring(L, -1)); \
  } \
  if(lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) { \
    snow_fail("%s", lua_tostring(L, -1)); \
  } \
} while(0)

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

void lua_add_required_module(lua_State *L, const char *name, const char *code);

#endif //__SHUTTLESOCK_TEST_H
