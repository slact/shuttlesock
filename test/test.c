#include "test.h"
#ifndef __clang_analyzer__
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

static bool tm_init(shuso_t *S, shuso_module_t *self) {
  return true;
}

describe(modules) {
  static shuso_t *S = NULL;
  shuso_module_t test_module;
  
  subdesc(bad_modules) {
  
    before_each() {
      test_module = (shuso_module_t ){
        .name = "tm",
        .version="0.0.0",
        .initialize = tm_init
      };
      assert(test_module.publish == NULL);
      assert(test_module.subscribe == NULL);
      S = shuso_create(NULL);
      //shuso_set_log_fd(S, dev_null);
    }
    after_each() {
      if(S) shuso_destroy(S);
    }
    
    test("bad subscribe string") {
      test_module.subscribe = "!@#@#$$%#";
      shuso_add_module(S, &test_module);
      shuso_configure_finish(S);
      assert_shuso_error(S, "invalid character .+ in subscribe string");
    }
    test("subscribe to nonexistent module's event") {
      test_module.subscribe = "fakemodule:start_banana";
      shuso_add_module(S, &test_module);
      shuso_configure_finish(S);
      assert_shuso_error(S, "module %w+ was not found");
    }
    test("subscribe to malformed event") {
      test_module.subscribe = "fakemodule:what:nothing another:malformed:event";
      shuso_add_module(S, &test_module);
      shuso_configure_finish(S);
      assert_shuso_error(S, "invalid value \".+\" in subscribe string");
    }
    test("publish malformed event name") {
      test_module.publish = "no:this_is_wrong";
      shuso_add_module(S, &test_module);
      shuso_configure_finish(S);
      assert_shuso_error(S, "invalid event name .+ in publish string");
    }
    test("depend on nonexistent module") {
      test_module.parent_modules = "foobar baz";
      shuso_add_module(S, &test_module);
      shuso_configure_finish(S);
      assert_shuso_error(S, "module .+ was not found");
    }
    test("leave published events uninitialized") {
      test_module.publish = "foobar bar baz";
      shuso_add_module(S, &test_module);
      shuso_configure_finish(S);
      assert_shuso_error(S, "module .+ has %d+ uninitialized events");
    }
  }
}

describe(init_and_shutdown) {
  static shuso_t          *S = NULL;
  static test_runcheck_t  *chk = NULL;
  before_each() {
    S = shusoT_create(&chk, 0.5);
  }
  after_each() {
    if(S) {
      shuso_destroy(S);
      if(chk)
        shmfree(chk);
      S = NULL;
    }
  }
  test("run loop") {
    chk->ctx.timeout_is_ok = true;
    shuso_configure_finish(S);
    shuso_run(S);
    assert_shuso_ok(S);
  }
  
  skip("stop from manager") {
    shuso_add_timer_watcher(S, 0.5, 0.0, stop_timer, (void *)(intptr_t)SHUTTLESOCK_MANAGER);
    shuso_run(S);
    assert_shuso(S);
  }
}


#define IPC_ECHO 130

typedef struct {
  int   active;
  int   procnum;
  int   barrage;
  int   barrage_received;
  int   seq;
  int   seq_received;
  _Atomic(bool) init_sleep_flag;
  float sleep;
  float slept;
  
} ipc_check_oneway_t;

typedef struct {
  _Atomic(bool)       failure;
  char                err[1024];
  _Atomic(int)        sent;
  _Atomic(int)        received;
  int                 received_stop_at;
  float               sleep_step;
  
  ipc_check_oneway_t ping;
  ipc_check_oneway_t pong;
} ipc_check_t;


typedef struct {
  _Atomic(bool)       failure;
  char                err[1024];
  _Atomic(int)        sent;
  _Atomic(int)        received;
  
  ipc_check_oneway_t  manager;
  ipc_check_oneway_t  worker[16];
  
} ipc_one_to_many_check_t;


#define check_ipc(test, S, chk, ...) \
do { \
  if(!(test)) { \
    chk->failure = true;\
    sprintf(chk->err, __VA_ARGS__); \
    shuso_stop(S, SHUSO_STOP_INSIST); \
    return; \
  } \
} while(0)

void ipc_echo_srcdst(shuso_t *S, ipc_check_oneway_t **self, ipc_check_oneway_t **dst) {
  ipc_check_t   *chk = S->data;
  if(S->procnum == chk->ping.procnum) {
    *self = &chk->ping;
    *dst = &chk->pong;
  }
  else if(S->procnum == chk->pong.procnum) {
    *self = &chk->pong;
    *dst = &chk->ping;
  } 
}

void ipc_echo_send(shuso_t *S) {
  ipc_check_t   *chk = S->data;
  
  ipc_check_oneway_t *self = NULL, *dst = NULL;
  ipc_echo_srcdst(S, &self, &dst);
  
  float sleeptime = 0;
  if(self->sleep > 0 && self->slept < self->sleep) {
    sleeptime = chk->sleep_step == 0 ? self->sleep : chk->sleep_step;
    self->slept -= sleeptime;
  }
  if(sleeptime > 0) {
    ev_sleep(sleeptime);
  }
  
  self->barrage_received = 0;
  
  shuso_process_t *processes = S->common->process.worker;
  shuso_process_t *dst_process = &processes[dst->procnum];
  
  for(int i=0; i<dst->barrage; i++) {
    if(chk->sent >= chk->received_stop_at) {
      //we're done;
      return;
    }
    dst->seq++;
    chk->sent++;
    bool sent_ok = shuso_ipc_send(S, dst_process, IPC_ECHO, (void *)(intptr_t )dst->seq);
    check_ipc(sent_ok, S, chk, "failed to send ipc message to procnum %d", dst->procnum);
  }
  if(dst->init_sleep_flag) {
    dst->init_sleep_flag = 0;
  }
}

void ipc_echo_receive(shuso_t *S, const uint8_t code, void *ptr) {
  ipc_check_oneway_t *self = NULL, *dst = NULL;
  ipc_echo_srcdst(S, &self, &dst);
  
  ipc_check_t   *chk = S->data;
  intptr_t       seq = (intptr_t )ptr;
  chk->received++;
  self->seq_received++;
  //shuso_log(S, "received %d", self->seq_received);
  check_ipc(chk->received <= chk->sent, S, chk, "sent - received mismatch for procnum %d: send %d > received %d", self->procnum, chk->sent, chk->received);
  check_ipc(seq == self->seq_received, S, chk, "seq mismatch for procnum %d: expected %d, got %ld", self->procnum, self->seq, seq);
  
  if(chk->received >= chk->received_stop_at) {
    //we're done;
    //shuso_log(S, "it's time to stop");
    shuso_stop(S, SHUSO_STOP_INSIST); \
    return;
  }
  
  self->barrage_received++;
  if(self->barrage_received < self->barrage) {
    //don't reply yet
    return;
  }
  self->barrage_received = 0;
  
  ipc_echo_send(S);
}

static void ipc_load_test(EV_P_ shuso_ev_timer *w, int rev) {
  shuso_t *S = ev_userdata(EV_A);
  ipc_check_t   *chk = S->data;
  ipc_check_oneway_t *self = NULL, *dst = NULL;
  ipc_echo_srcdst(S, &self, &dst);
  
  while(self->init_sleep_flag) {
    ev_sleep(0.05);
  }
  
  float sleeptime = 0;
  if(self->sleep > 0 && self->slept < self->sleep) {
    sleeptime = chk->sleep_step == 0 ? self->sleep : chk->sleep_step;
    self->slept -= sleeptime;
  }
  if(sleeptime > 0) {
    shuso_log_notice(S, "sleep %f", sleeptime);
    ev_sleep(sleeptime);
  }
  
  if(!shuso_is_master(S)) {
    return;
  }
  assert(chk->ping.procnum == SHUTTLESOCK_MANAGER);
  ipc_echo_send(S);
}
#undef check_ipc

void ipc_echo_cancel(shuso_t *S, const uint8_t code, void *ptr) { }

describe(ipc) {
  static shuso_t *S = NULL;
  subdesc(one_to_one) {
  static ipc_check_t *ipc_check = NULL;
    before_each() {
      S = NULL;
      ipc_check = shmalloc(ipc_check);
      S = runcheck_shuso_create();
      S->data = ipc_check;
    }
    after_each() {
      if(S) {
        test_runcheck_t *runcheck = S->common->phase_handlers.privdata;
        shuso_destroy(S);
        if(runcheck) {
          shmfree(runcheck);
        }
        S = NULL;
      }
      if(ipc_check) {
        shmfree(ipc_check);
      }
    }
    test("simple round-trip") {
      ipc_check->received_stop_at = 1000;
      ipc_check->ping.procnum = SHUTTLESOCK_MANAGER;
      ipc_check->pong.procnum = SHUTTLESOCK_MASTER;
      ipc_check->ping.barrage = 1;
      ipc_check->pong.barrage = 1;

      shuso_add_timer_watcher(S, 0.1, 0.0, ipc_load_test, NULL);
      shuso_ipc_add_handler(S, "echo", IPC_ECHO, ipc_echo_receive, ipc_echo_cancel);
      shuso_run(S);
      assert_shuso(S);
    }
    
    test("one-sided round-trip (250:1)") {
      ipc_check->received_stop_at = 1000;
      ipc_check->ping.procnum = SHUTTLESOCK_MANAGER;
      ipc_check->pong.procnum = SHUTTLESOCK_MASTER;
      ipc_check->ping.barrage = 250;
      ipc_check->pong.barrage = 1;

      shuso_add_timer_watcher(S, 0.1, 0.0, ipc_load_test, NULL);
      shuso_ipc_add_handler(S, "echo", IPC_ECHO, ipc_echo_receive, ipc_echo_cancel);
      shuso_run(S);
      assert_shuso(S);
    }
    
    test("buffer fill (500:1)") {
      ipc_check->received_stop_at = 10000;
      ipc_check->ping.procnum = SHUTTLESOCK_MANAGER;
      ipc_check->pong.procnum = SHUTTLESOCK_MASTER;
      ipc_check->ping.barrage = 400;
      ipc_check->pong.barrage = 1;
      ipc_check->ping.init_sleep_flag = 1;

      shuso_add_timer_watcher(S, 0.1, 0.0, ipc_load_test, NULL);
      shuso_ipc_add_handler(S, "echo", IPC_ECHO, ipc_echo_receive, ipc_echo_cancel);
      shuso_run(S);
      assert_shuso(S);
    }
  }
  
  subdesc(many_to_one) {
    static ipc_one_to_many_check_t *ipc_check = NULL;
    before_each() {
      S = NULL;
      ipc_check = shmalloc(ipc_check);
      S = runcheck_shuso_create();
      S->data = ipc_check;
    }
    after_each() {
      if(S) {
        test_runcheck_t *runcheck = S->common->phase_handlers.privdata;
        shuso_destroy(S);
        if(runcheck) {
          shmfree(runcheck);
        }
        S = NULL;
      }
      if(ipc_check) {
        shmfree(ipc_check);
      }
    }
    skip("4 workers to manager") {

    }
    
  }
}
#define MEM_DEFINED(addr) \
 (VALGRIND_CHECK_MEM_IS_ADDRESSABLE(addr) == 0 && VALGRIND_CHECK_MEM_IS_DEFINED(addr) == 0)

describe(stack_allocator) {
  static shuso_stalloc_t st;
  before_each() {
    shuso_stalloc_init(&st, 0);
  }
  after_each() {
    shuso_stalloc_empty(&st);
  }
  
  test("page header alignment") {
    asserteq(sizeof(shuso_stalloc_page_t) % sizeof(void *), 0, "page header struct must be native pointer size aligned");
  }
  test("handful of pointer-size allocs") {
    void *ptr[10];
    for(int i=0; i<10; i++) {
      ptr[i] = shuso_stalloc(&st, sizeof(void *));
      ptr[i] = (void *)(intptr_t)i;
      ptr[i] = &((char *)&ptr[i])[1];
      for(int j=0; j<i; j++) {
        assertneq(ptr[i], ptr[j], "those should be different allocations");
      }
    }
    asserteq(st.allocd.last, NULL, "nothing allocd");
  }
  
  test("some very large allocs") {
    char *chr[10];
    size_t sz = st.page.size + 10;
    for(int i=0; i<10; i++) {
      chr[i] = shuso_stalloc(&st, sz);
      memset(chr[i], 0x12, sz);
      for(int j=0; j<i; j++) {
        assertneq((void *)chr[i], (void *)chr[j], "those should be different allocations");
      }
      assertneq(st.allocd.last, NULL, "alloc shouldn't be NULL");
      asserteq((void *)st.allocd.last->data, (void *)chr[i], "wrong last alloc");
    }
  }
  
  test("a few pages' worth") {
    static char *chr[500];
    size_t sz = st.page.size / 5;
    for(int i=0; i<500; i++) {
      chr[i] = shuso_stalloc(&st, sz);
      memset(chr[i], 0x12, sz);
      for(int j=0; j<i; j++) {
        assertneq((void *)chr[i], (void *)chr[j], "those should be different allocations");
      }
      asserteq(st.allocd.last, NULL, "nothing allocd");
    }
    assert(st.page.count>1, "should have more than 1 page");
  }
  subdesc(stack) {
    static test_stalloc_stats_t stats;
    before_each() {
      shuso_stalloc_init(&st, 0);
      memset(&stats, 0x00, sizeof(stats));
    }
    after_each() {
      shuso_stalloc_empty(&st);
    }
    
    
    test("push") {
      fill_stalloc(&st, &stats, 1, 256, 30, 1000, 8);
    }
    
    test("push/pop") {
      fill_stalloc(&st, &stats, 1, 256, 30, 1000, 8);
      shuso_stalloc_pop_to(&st, 3);
      assert(st.allocd.last == stats.stack[2].allocd);
      assert(st.page.last == stats.stack[2].page);
      assert(st.page.cur == stats.stack[2].page_cur);
      shuso_stalloc_pop_to(&st, 0);
    }
  }
  
  /*subdesc(space_tracking) {
    test("track space cumulatively") {
      
    }
    
    test("track space after stack manupulation") {
      
    }
  }*/
}

void resolve_check_ok(shuso_t *S, shuso_resolver_result_t result, struct hostent *hostent, void *pd) {
  assert(result == SHUSO_RESOLVER_SUCCESS);
  //printf("Found address name %s\n", hostent->h_name);
  char ip[INET6_ADDRSTRLEN];
  int i = 0;
  for (i = 0; hostent->h_addr_list[i]; ++i) {
    inet_ntop(hostent->h_addrtype, hostent->h_addr_list[i], ip, sizeof(ip));
    //printf("%s\n", ip);
  }
  shuso_stop(S, SHUSO_STOP_INSIST);
}

void resolve_check_start(EV_P_ shuso_ev_timer *w, int revent) {
  shuso_t *S = ev_userdata(EV_A);
  if(S->procnum != SHUTTLESOCK_MANAGER) {
    return;
  }
  shuso_resolve_hostname(&S->resolver, "google.com", AF_INET, resolve_check_ok, S);
}

describe(resolver) {
   static shuso_t *S = NULL;
  before_each() {
    S = NULL;
  }
  after_each() {
    if(S) {
      shuso_destroy(S);
      S = NULL;
    }
  }
  test("resolve using system") {
    S = runcheck_shuso_create();
    
    shuso_add_timer_watcher(S, 0.01, 0.0, resolve_check_start, 0);
    
    shuso_run(S);
    assert_shuso(S);
  }
}

typedef struct {
  void    *ptr;
  uint32_t sz;
  unsigned free:1;
} allocd_t;

describe(shared_memory_allocator) {
  static shuso_t *S = NULL;
  static shuso_shared_slab_t shm;
  before_each() {
    shuso_system_initialize();
    S = runcheck_shuso_create();
  }
  after_each() {
    shuso_destroy(S);
    S = NULL;
  }
  test("single-threaded alloc/free") {
    size_t shm_sz = 10*1024*1024;
    unsigned total_allocs = 5000;
    unsigned max_size = 1000;
    unsigned allocs_before_free = total_allocs/3;
    uint64_t i;
    for(int rep =0; rep < 10; rep++) {
      allocd_t *allocd = calloc(sizeof(allocd_t), total_allocs);
      assert(allocd);
      assert(shuso_shared_slab_create(S, &shm, shm_sz, "single-threaded alloc/free test"));
      for(i = 0; i < total_allocs; i++) {
        size_t sz = rand() % max_size + 1;
        allocd[i].free = 0;
        allocd[i].ptr = shuso_shared_slab_calloc(&shm, sz);
        memset(allocd[i].ptr, ((uintptr_t )allocd[i].ptr) % 0x100, sz);
        allocd[i].sz = sz;
        assert(allocd[i].ptr);
        if(i > allocs_before_free) {
          int ifree = i - 100;
          assert(allocd[ifree].free == 0, "shouldn't have been freed yet");
          assert(allocd_ptr_value_correct(allocd[ifree].ptr, allocd[ifree].sz), "correct value, hasn't been overwritten");
          shuso_shared_slab_free(&shm, allocd[ifree].ptr);
          allocd[ifree].free = 1;
        }
      }
      
      for(i=0; i < total_allocs; i++) {
        if(!allocd[i].free) {
          assert(allocd_ptr_value_correct(allocd[i].ptr, allocd[i].sz));
          shuso_shared_slab_free(&shm, allocd[i].ptr);
          allocd[i].free = 1;
        }
      }
      
      free(allocd);
    }
  }
}

typedef struct {
  const char      *err;
  uint16_t         port;
  uint16_t         num_ports;
} listener_port_test_t;

void listener_port_test_runner_callback(shuso_t *S, shuso_status_t status, shuso_hostinfo_t *hostinfo, int *sockets, int socket_count, void *pd) {
  listener_port_test_t *t = pd;
  if(status == SHUSO_OK) {
    t->err = NULL;
  }
  else if(status == SHUSO_FAIL) {
    t->err = "listener port creation failed";
  }
  else if(status == SHUSO_TIMEOUT) {
    t->err = "listener port creation timed out";
  }
  //TODO: actually check the sockets
  for(int i=0; i< socket_count; i++) {
    if(sockets[i] == -1) {
      t->err = "unexpected invalid socket found";
    }
    if(close(sockets[i]) == -1) {
      switch(errno) {
        case EBADF:
          t->err = "bad fine decriptor";
          break;
        case EIO:
          t->err = "I/O error cleaning up file descriptor";
          break;
      }
    }
    sockets[i]=-1;
  }
  //shuso_log_notice(S, "wrapping it up");
  shuso_stop(S, SHUSO_STOP_INSIST);
}

void listener_port_test_runner(EV_P_ shuso_ev_timer *w, int revent) {
  shuso_t *S = ev_userdata(EV_A);
  listener_port_test_t *pt = w->ev.data;
  if(S->procnum != SHUTTLESOCK_MANAGER) {
    return;
  }
  shuso_hostinfo_t host = {
    .name="test",
    .addr_family=AF_INET,
    .port = pt->port,
    .udp = 0
  };
  
  shuso_sockopt_t sopt[10] = {
    { 
      .level = SOL_SOCKET,
      .name = SO_REUSEPORT,
      .value.integer = 1
    }
  };
  shuso_sockopts_t opts = {
    .count = 1,
    .array = sopt
  };
  bool rc = shuso_ipc_command_open_listener_sockets(S, &host, 5, &opts, listener_port_test_runner_callback, pt);
  assert(rc);
}

describe(listener_sockets) {
    static shuso_t *S = NULL;
    static listener_port_test_t *pt = NULL;
  before_each() {
    shuso_system_initialize();
    S = runcheck_shuso_create();
    pt = shmalloc(pt);
    pt->err = NULL;
  }
  after_each() {
    shuso_destroy(S);
    shmfree(pt);
    S = NULL;
  }
  test("listen on port 34241") {
    pt->port = 34241;
    shuso_add_timer_watcher(S,  0.1, 0.0, listener_port_test_runner, pt);
    shuso_run(S);
    printf("heyo\n");
    assert_shuso(S);
    if(pt->err != NULL) {
      snow_fail("%s", pt->err);
    }
  }
}

snow_main_decls;
int main(int argc, char **argv) {
  _snow.ignore_unknown_options = 1;
  memset(&test_config, 0x0, sizeof(test_config));
  dev_null = open("/dev/null", O_WRONLY);
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
  close(dev_null);
  fclose(stdin);

  fclose(stdout);

  fclose(stderr); 
  return rc;
}

#endif //__clang_analyzer__
