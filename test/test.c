#include "test.h"

bool set_test_options(int *argc, char **argv) {
  snow_set_extra_help(""
    "    --verbose:      Verbose test output.\n"
  );
  int i = 1;
  while(i < *argc) {
    char *arg = argv[i];
    if(strcmp(arg, "--verbose") == 0) {
      test_config.verbose = true;
    }
    i++;
  }
  return true;
}



describe(shuttlesock_init_and_shutdown) {
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
    shuso_add_timer_watcher(ss, stop_timer, (void *)(intptr_t)SHUTTLESOCK_MASTER, 0.2, 0.0);
    shuso_run(ss);
    assert_shuso(ss);
  }
  
  test("stop from manager") {
    ss = runcheck_shuso_create(EVFLAG_AUTO, NULL);
    shuso_add_timer_watcher(ss, stop_timer, (void *)(intptr_t)SHUTTLESOCK_MANAGER, 0.2, 0.0);
    shuso_run(ss);
    assert_shuso(ss);
  }
}


#define IPC_ECHO 130

typedef struct {
  int   procnum;
  int   barrage_received;
  float sleep;
  float slept;
  
} ipc_check_oneway_t;

typedef struct {
  _Atomic(bool)       failure;
  char                err[1024];
  _Atomic(int)        sent;
  _Atomic(int)        received;
  _Atomic(intptr_t)   seq;
  int                 seq_stop_at;
  int                 barrage;
  float               sleep_step;
  
  ipc_check_oneway_t ping;
  ipc_check_oneway_t pong;
} ipc_check_t;

static void ipc_load_test(EV_P_ ev_timer *w, int rev) {
  shuso_t *ctx = ev_userdata(EV_A);
  if(!shuso_is_master(ctx)) {
    return;
  }
  ipc_check_t   *chk = ctx->data;
  chk->sent++;
  assert(chk->ping.procnum == SHUTTLESOCK_MANAGER);
  shuso_ipc_send(ctx, &ctx->common->process.manager, IPC_ECHO, (void *)(intptr_t )0);
}
#define check_ipc(test, ctx, chk, ...) \
do { \
  if(!(test)) { \
    chk->failure = true;\
    sprintf(chk->err, __VA_ARGS__); \
    shuso_stop(ctx, SHUSO_STOP_INSIST); \
    return; \
  } \
} while(0)

void ipc_echo_receive(shuso_t *ctx, const uint8_t code, void *ptr) {
  ipc_check_t   *chk = ctx->data;
  intptr_t       seq = (intptr_t )ptr;
  chk->received++;
  
  check_ipc(chk->received == chk->sent, ctx, chk, "sent - received mismatch: send %d received %d", chk->sent, chk->received);
  check_ipc(seq == chk->seq, ctx, chk, "seq mismatch: expected %ld, got %ld", chk->seq, seq);
  chk->seq++;
  
  if(chk->seq >= chk->seq_stop_at) {
    shuso_stop(ctx, SHUSO_STOP_INSIST);
    return;
  }
  shuso_process_t *procs = ctx->common->process.worker;
  int reply_procnum;
  ipc_check_oneway_t *self;
  if(ctx->procnum == chk->ping.procnum) {
    self = &chk->ping;
    reply_procnum = chk->pong.procnum;
  }
  else if(ctx->procnum == chk->pong.procnum) {
    self = &chk->pong;
    reply_procnum = chk->ping.procnum;
  }
  else {
    check_ipc(false, ctx, chk, "am i pinging or ponging?");
  }
  shuso_process_t *dst = &procs[reply_procnum];
  
  float sleeptime = 0;
  if(self->sleep > 0 && self->slept < self->sleep) {
    sleeptime = chk->sleep_step == 0 ? self->sleep : chk->sleep_step;
    self->slept -= sleeptime;
  }
  if(sleeptime > 0) {
    ev_sleep(sleeptime);
  }

  
  self->barrage_received++;
  if(self->barrage_received < (chk->barrage ? chk->barrage : 1)) {
    //don't reply yet
    return;
  }
  self->barrage_received = 0;
  chk->sent++;
  bool sent_ok = shuso_ipc_send(ctx, dst, IPC_ECHO, (void *)chk->seq);
  check_ipc(sent_ok, ctx, chk, "failed to send ipc message to procnum %d", reply_procnum);
}
#undef check_ipc

void ipc_echo_cancel(shuso_t *ctx, const uint8_t code, void *ptr) { }

describe(ipc) {
  static shuso_t *ss = NULL;
  static ipc_check_t *ipc_check = NULL;
  
  before_each() {
    ss = NULL;
    ipc_check = shmalloc(ipc_check);
    ss = runcheck_shuso_create(EVFLAG_AUTO, NULL);
    ss->data = ipc_check;
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
    if(ipc_check) {
      shmfree(ipc_check);
    }
  }
  test("simple round-trip") {
    ipc_check->ping.procnum = SHUTTLESOCK_MANAGER;
    ipc_check->pong.procnum = SHUTTLESOCK_MASTER;
    ipc_check->seq_stop_at = 1000;
    ipc_check->barrage = 1;

    shuso_add_timer_watcher(ss, ipc_load_test, NULL, 0.1, 0.0);
    shuso_ipc_add_handler(ss, "echo", IPC_ECHO, ipc_echo_receive, ipc_echo_cancel);
    shuso_run(ss);
    assert_shuso(ss);
  }
  
  test("one-sided round-trip (2:1)") {
    ipc_check->ping.procnum = SHUTTLESOCK_MANAGER;
    ipc_check->pong.procnum = SHUTTLESOCK_MASTER;
    ipc_check->seq_stop_at = 100;
    ipc_check->barrage = 1;

    shuso_add_timer_watcher(ss, ipc_load_test, NULL, 0.1, 0.0);
    shuso_ipc_add_handler(ss, "echo", IPC_ECHO, ipc_echo_receive, ipc_echo_cancel);
    shuso_run(ss);
    assert_shuso(ss);
  }
}

snow_main_decls;
int main(int argc, char **argv) {
  _snow.ignore_unknown_options = 1;
  memset(&test_config, 0x0, sizeof(test_config));
  dev_null = open("/dev/null", O_WRONLY);
  child_result = shmalloc(child_result);
  assert(child_result);
  printf("yeah\n");
  if(!set_test_options(&argc, argv)) {
    return 1;
  }
  printf("yeah\n");
  pid_t pid = getpid();
  int rc = snow_main_function(argc, argv);
  if(getpid() != pid) {
    child_result->pid = getpid();
    child_result->status = rc;
  }
  shmfree(child_result);
  close(dev_null);
  return rc;
}
