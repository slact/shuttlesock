#include <shuttlesock.h>
#include <shuttlesock/log.h>
#include <stdio.h>
#define SNOW_ENABLED 1
#include "snow.h"
#include <signal.h>
#include <sys/mman.h>

#undef snow_main_decls
#define snow_main_decls \
        void snow_break() {} \
        void snow_rerun_failed() {raise(SIGSTOP);} \
        struct _snow _snow; \
        int _snow_inited = 0

#include "test.h"

int set_test_options(int *argc, char **argv) {
  int i = 1;
  while(i < *argc) {
    char *arg = argv[i];
    /*
    if(strcmp(arg, "--multiplier") == 0 && *argc >= i+1) {
      char *val = argv_extract2(argc, argv, i);
      if((repeat_multiplier = atof(val)) == 0.0) {
        printf("invalid --multiplier value %s\n", val);
        return 0;
      }
    }
    else {
      i++;
    }
    */
  }
  return 1;
}

void start_master(shuso_t *ctx, void *pd) {
  shuso_log(ctx, "start master");
}
void stop_master(shuso_t *ctx, void *pd) {
  shuso_log(ctx, "stop master");
}
void start_manager(shuso_t *ctx, void *pd) {
  shuso_log(ctx, "start manager");
}
void stop_manager(shuso_t *ctx, void *pd) {
  shuso_log(ctx, "stop manager");
}
void start_worker(shuso_t *ctx, void *pd) {
  shuso_log(ctx, "start worker");
}
void stop_worker(shuso_t *ctx, void *pd) {
  shuso_log(ctx, "stop worker");
}

describe(shuttlesock_init) {
  static shuso_t *ss = NULL;
  before_each() {
    ss = NULL;
  }
  after_each() {
    if(ss) {
      shuso_destroy(ss);
      ss = NULL;
    }
  }
  test("run loop") {
    shuso_handlers_t handlers = {
      .start_master = start_master,
      .stop_master = stop_master,
      .start_manager = start_manager,
      .stop_manager = stop_manager,
      .start_worker = start_worker,
      .stop_worker = stop_worker
    };
    ss = shuso_create(EVFLAG_AUTO, &handlers, NULL, NULL);
    shuso_run(ss);
    assert_shuttlesock_ok(ss);
    if(shuso_is_forked_manager(ss)) {
      sleep(2);
    }
  }
  test("another test") {
    //do nothing
  }
  subdesc(oh_no_athother_thing) {
    test("test thisn thing too") {
      //meh
    }
  }
}

describe(now_what) {
  test("yeap...") {
    //okay
  }
}

snow_main_decls;
int main(int argc, char **argv) {
  child_result = mmap(NULL, sizeof(*child_result), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED,-1, 0);
  assert(child_result);
  if(!set_test_options(&argc, argv)) {
    return 1;
  }
  pid_t pid = getpid();
  int rc = snow_main_function(argc, argv);
  if(getpid() != pid) {
    child_result->pid = getpid();
    child_result->status = rc;
  }
  munmap(child_result, sizeof(child_result));
  return rc;
}
