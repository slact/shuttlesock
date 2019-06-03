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



describe(shuttlesock_init) {
  static shuso_t *ss = NULL;
  before_each() {
    ss = NULL;
  }
  after_each() {
    if(ss) {
      test_runcheck_t *runcheck = ss->common->phase_handlers.privdata;
      shuso_destroy(ss);
      if(runcheck) {
        shmfree(runcheck);
      }
      ss = NULL;
    }
  }
  test("run loop") {
    ss = runcheck_shuso_create(EVFLAG_AUTO, NULL);
    shuso_run(ss);
    test_runcheck_t *chk = ss->common->phase_handlers.privdata;
    if(!shuso_is_forked_manager(ss)) {
      //meh
    }
    assert_shuso(ss);
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
  child_result = shmalloc(child_result);
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
  shmfree(child_result);
  return rc;
}
