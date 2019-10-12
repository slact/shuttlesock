#include "test.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
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

describe(modules) {
  static shuso_t *S = NULL;
  static shuso_module_t test_module;
  
  subdesc(bad_modules) {
  
    before_each() {
      test_module = (shuso_module_t ){
        .name = "tm",
        .version="0.0.0"
      };
      assert(test_module.publish == NULL);
      assert(test_module.subscribe == NULL);
      S = shuso_create(NULL);
      if(!test_config.verbose) {
        shuso_set_log_fd(S, dev_null);
      }
    }
    after_each() {
      if(S) shuso_destroy(S);
    }
    
    test("bad subscribe string") {
      test_module.subscribe = "chirp chhorp !@#@#$$%#";
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
    test("subscribe to nonexistent event of real module") {
      test_module.subscribe = "core:start_banana";
      shuso_add_module(S, &test_module);
      shuso_configure_finish(S);
      assert_shuso_error(S, "does not publish .*event");
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
  subdesc(lua) {
    before_each() {
      S = shuso_create(NULL);
      if(!test_config.verbose) {
        shuso_set_log_fd(S, dev_null);
      }
    }
    after_each() {
      if(S) shuso_destroy(S);
    }
    test("a lua module") {
      lua_State *L = S->lua.state;
      assert_luaL_dostring(L,"\
        local Module = require 'shuttlesock.module' \
        local m = assert(Module.new{ \
          name='luatest', \
          version='0.0.0', \
          publish={'foo', 'bar'}, \
          subscribe={'core:worker.workers_started', 'core:worker.stop'} \
        }) \
        assert(m:add())"
      );
      //assert_shuso(S, shuso_configure_finish(S));
      
      //assert_luaL_dostring(L, "require 'shuttlesock.module'.find('luatest')");
    }
    
  }
}

static void stop_shuttlesock(shuso_t *S, void *pd) {
  intptr_t procnum = (intptr_t)pd;
  assert(S->procnum == procnum);
  shuso_stop(S, SHUSO_STOP_ASK);
  
}

describe(init_and_shutdown) {
  static shuso_t          *S = NULL;
  static test_runcheck_t  *chk = NULL;
  before_each() {
    S = shusoT_create(&chk, 25);
  }
  after_each() {
    shusoT_destroy(S, &chk);
  }
  test("lua stack doesn't grow") {  
    assert(lua_gettop(S->lua.state) == 0);
    shuso_configure_finish(S);
    assert(lua_gettop(S->lua.state) == 0);
  }
  test("run loop, stop from manager") {
    shuso_configure_finish(S);
    shusoT_run_test(S, SHUTTLESOCK_MANAGER, stop_shuttlesock, NULL, (void *)(intptr_t)SHUTTLESOCK_MANAGER);
    assert_shuso_ran_ok(S);
  }
  
  test("stop from master") {
    shuso_configure_finish(S);
    shusoT_run_test(S, SHUTTLESOCK_MASTER, stop_shuttlesock, NULL, (void *)(intptr_t)SHUTTLESOCK_MASTER);
    assert_shuso_ran_ok(S);
  }
  test("stop from worker") {
    shuso_configure_finish(S);
    shusoT_run_test(S, 0, stop_shuttlesock, NULL, (void *)(intptr_t)0);
    assert_shuso_ran_ok(S);
  }
}

static bool config_test_a_setting_init_config(shuso_t *S, shuso_module_t *module, shuso_setting_block_t *block) {
  return true;
}

describe(config) {
  static shuso_module_t    test_module;
  static test_runcheck_t  *chk = NULL;
  static shuso_t          *S = NULL;
  before_each() {
    S = shusoT_create(&chk, 25);
    test_module = (shuso_module_t ){
      .name = "tm",
      .version="0.0.0",
    };
  }
  after_each() {
    shusoT_destroy(S, &chk);
  }
  test("a setting please") {
    test_module.initialize_config = config_test_a_setting_init_config;
    test_module.settings = (shuso_module_setting_t []){
      { 
        .name="foobar",
        .path="/",
        .description="yello",
        .nargs="1..30",
        .default_value="42",
        .block=false
      },
      SHUTTLESOCK_SETTINGS_END
    };
    shuso_add_module(S, &test_module);
    shuso_config_string_parse(S, " \
      foobar 10 11 12 13 \"14\"; \n\
      blorp { \n\
        shmoo { \n\
          blorp { \n\
            flarb 99; \n\
            #yeah \n\
            foobar 12; \n\
          }\n\
        }\n\
      }\n\
    ");
    shuso_configure_finish(S);
    shusoT_run_test(S, SHUTTLESOCK_MANAGER, stop_shuttlesock, NULL, (void *)(intptr_t)SHUTTLESOCK_MANAGER);
    assert_shuso_ran_ok(S);
  }
}

describe(lua_bridge) {
  subdesc(gxcopy) {
    static shuso_t   *Ss;
    static shuso_t   *Sd;
    static lua_State *Ls;
    static lua_State *Ld;
    before_each() {
      Ss = shuso_create(NULL);
      Sd = shuso_create(NULL);
      Ls = Ss->lua.state;
      Ld = Sd->lua.state;
      
      assert(Ls);
      assert(Ld);
    }
    after_each() {
      if(Ss) shuso_destroy(Ss);
      if(Sd) shuso_destroy(Sd);
    }
    
    test("self-referencing tables") {
      assert_luaL_dostring(Ls,"\
        local t={11,22,33,44,55} \
        t[2] = t \
        t[3] = t \
        t[t] = t \
        return t"
      );
      assert(luaS_gxcopy_start(Ls, Ld));
      assert(luaS_gxcopy(Ls, Ld));
      assert(luaS_gxcopy_finish(Ls, Ld));
      shuso_destroy(Ss);
      Ss = NULL;
      lua_setglobal(Ld, "copy");
      assert_luaL_dostring(Ld, "\
        assert(copy[copy] == copy, 'self-reference broken') \
        assert(copy[1] == 11) \
        assert(copy[2] == copy)"
      );
    }
    
    test("function with upvalues") {
      lua_pushinteger(Ls, 500);
      lua_pushinteger(Ld, 500);
      int stacksize_s = lua_gettop(Ls);
      int stacksize_d = lua_gettop(Ld);
      
      assert_luaL_dostring(Ls,"\
        local x = 11 \
        local y = 9 \
        local z = 100 \
        local function foo() \
          return x+y+z \
        end \
        return foo"
      );
      assert(luaS_gxcopy_start(Ls, Ld));
      assert(luaS_gxcopy(Ls, Ld));
      assert(luaS_gxcopy_finish(Ls, Ld));
      assert(lua_gettop(Ls) == stacksize_s+1);
      assert(lua_gettop(Ld) == stacksize_d+1);
      
      assert_lua_call(Ld, 0, 1);
      assert_lua_call(Ls, 0, 1);
      
      assert(lua_tointeger(Ls, -1) == 120);
      assert(lua_tointeger(Ld, -1) == 120);
    }
    
    test("required packages") {
      lua_add_required_module(Ls, "foobar", "return {o='src'}");
      assert_luaL_dostring(Ls,"\
        local foobar = require 'foobar' \
        return { \
          foo1 = foobar, \
          foo2 = foobar, \
          math = require 'math', \
          metafoo = setmetatable({1}, require 'foobar') \
        }"
      );
      
      lua_add_required_module(Ld, "foobar", "return {o='dst'}");
      
      assert(luaS_gxcopy_start(Ls, Ld));
      assert(luaS_gxcopy(Ls, Ld));
      assert(luaS_gxcopy_finish(Ls, Ld));
      lua_setglobal(Ld, "copy");
      
      assert_luaL_dostring(Ld, "\
        assert(copy.foo1 == require 'foobar', 'foo1 mismatch') \
        assert(copy.foo2 == require 'foobar', 'foo2 mismatch') \
        assert(copy.math == require 'math',  'math mismatch') \
        assert(getmetatable(copy.metafoo) == require 'foobar', 'metafoo metatable mismatch') \
        assert(require 'foobar'.o == 'dst', 'foobar module is wrong') \
        return true"
      );
      
      
      if(lua_isnil(Ld, -1)) {
        snow_fail("%s", lua_tostring(Ld, -2));
      }
    }
    
    test("shuttlesock.core.config") {
      assert_luaL_dostring(Ls,"\
        local foo = require('shuttlesock.core.config').new() \
        return foo \
      ");
      
      assert(luaS_gxcopy_start(Ls, Ld));
      assert(luaS_gxcopy(Ls, Ld));
      assert(luaS_gxcopy_finish(Ls, Ld));
      luaS_push_inspect_string(Ls, -1);
      luaS_push_inspect_string(Ld, -1);
      
      const char *str_src = lua_tostring(Ls, -1), *str_dst = lua_tostring(Ld, -1);
      asserteq_str(str_src, str_dst);
      lua_pop(Ls, 1);
      lua_pop(Ld, 1);
      
      lua_getmetatable(Ld, -1);
      assert_luaL_dostring(Ld, "return require('shuttlesock.core.config').metatable");
      assert(lua_compare(Ld, -1, -2, LUA_OPEQ) == 1);
    }
    
    test("recursive function") {
      assert_luaL_dostring(Ls, "\
        local function foo(x, y) \
          if x == 0 then return (y or 100) end \
          return foo(x-1, (y or 100)+1) \
        end \
        return foo \
      ");
      
      assert(luaS_gxcopy_start(Ls, Ld));
      assert(luaS_gxcopy(Ls, Ld));
      assert(luaS_gxcopy_finish(Ls, Ld));
      lua_pushvalue(Ld, -1);
      lua_pushinteger(Ld, 10);
      lua_call(Ld, 1, 1);
      assert(lua_tointeger(Ld, -1) == 110);
      lua_pop(Ld, 1);
    }
    
    test("multi-value, separate") {
      
      const char *str = "\
        local common = {common='common'} \
        local t1 = {x=1, common=common} \
        local t2 = {x=2, common=common} \
        return t1, t2 \
      ";
      
      assert_luaL_dostring(Ls, str);
      
      //copy the two values in separate gxcopy sessions
      assert(luaS_gxcopy_start(Ls, Ld));
      assert(luaS_gxcopy(Ls, Ld));
      assert(luaS_gxcopy_finish(Ls, Ld));
      lua_pop(Ls, 1);
      
      assert(luaS_gxcopy_start(Ls, Ld));
      assert(luaS_gxcopy(Ls, Ld));
      assert(luaS_gxcopy_finish(Ls, Ld));
      lua_pop(Ls, 1);
      
      lua_setglobal(Ld, "copy1");
      lua_setglobal(Ld, "copy2");
      
      
      assert_luaL_dostring(Ld, "\
        assert(copy1 ~= copy2, 'copy1 should not be same as copy2') \n\
        assert(copy1.x == 1, 'copy1.x == ' .. copy1.x) \n\
        assert(copy2.x == 2, 'copy2.x == ' .. copy2.x) \n\
        assert(copy1.common ~= copy2.common, 'common table should be a copy') \n\
      ");
      
      //and now copy the two values in the same gxcopy session
      assert_luaL_dostring(Ls, str);
      assert(luaS_gxcopy_start(Ls, Ld));
      assert(luaS_gxcopy(Ls, Ld));
      lua_pop(Ls, 1);
      assert(luaS_gxcopy(Ls, Ld));
      lua_pop(Ls, 1);
      assert(luaS_gxcopy_finish(Ls, Ld));
      lua_setglobal(Ld, "copy1");
      lua_setglobal(Ld, "copy2");
      
      assert_luaL_dostring(Ld, "\
        assert(copy1 ~= copy2, 'copy1 should not be same as copy2') \
        assert(copy1.x == 1, 'copy1.x == ' .. copy1.x) \
        assert(copy2.x == 2, 'copy2.x == ' .. copy2.x) \
        assert(copy1.common == copy2.common, 'common table should be the same') \
      ");
    }
  }
  subdesc(printstack) {
    static shuso_t   *S;
    static lua_State *L; 
    before_each() {
      S = shuso_create(NULL);
      L = S->lua.state;
      if(!test_config.verbose) {
        shuso_set_log_fd(S, dev_null);
      }
    }
    after_each() {
      if(S) shuso_destroy(S);
    }
    test("informative table information") {
      assert_luaL_dostring(L, "\
        _G['foobar'] = {} \
        _G['what'] = setmetatable({11}, {__name='beep'}) \
        return 1, 1.01, {'foo'} , foobar, what, _G, require('shuttlesock.module') \
      ");
      luaS_printstack(L);
    }
    
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

static void ipc_load_test(shuso_t *S, void *pd) {
  ipc_check_t   *chk = pd;
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
  
  assert(shuso_is_master(S));
  assert(chk->ping.procnum == SHUTTLESOCK_MANAGER);
  ipc_echo_send(S);
}

static void ipc_load_test_verify(shuso_t *S, void *pd) {
  ipc_check_t   *chk = pd;
  printf("chk->sent: %i\n", chk->sent);
}
#undef check_ipc

void ipc_echo_cancel(shuso_t *S, const uint8_t code, void *ptr) { }

describe(ipc) {
  static shuso_t          *S = NULL;
  static test_runcheck_t  *chk = NULL;
  
  subdesc(one_to_one) {
    static ipc_check_t *ipc_check = NULL;
    
    before_each() {
      S = shusoT_create(&chk, 25.0);
      ipc_check = shmalloc(ipc_check);
      S->data = ipc_check;
    }
    after_each() {
      shusoT_destroy(S, &chk);
      shmfree(ipc_check);
    }
    test("simple round-trip") {
      ipc_check->received_stop_at = 1000;
      ipc_check->ping.procnum = SHUTTLESOCK_MANAGER;
      ipc_check->pong.procnum = SHUTTLESOCK_MASTER;
      ipc_check->ping.barrage = 1;
      ipc_check->pong.barrage = 1;
      
      shuso_ipc_add_handler(S, "echo", IPC_ECHO, ipc_echo_receive, ipc_echo_cancel);
      shuso_configure_finish(S);
      shusoT_run_test(S, SHUTTLESOCK_MASTER, ipc_load_test, ipc_load_test_verify, ipc_check);
      assert_shuso_ran_ok(S);
    }
    
    test("one-sided round-trip (250:1)") {
      ipc_check->received_stop_at = 1000;
      ipc_check->ping.procnum = SHUTTLESOCK_MANAGER;
      ipc_check->pong.procnum = SHUTTLESOCK_MASTER;
      ipc_check->ping.barrage = 250;
      ipc_check->pong.barrage = 1;

      shuso_ipc_add_handler(S, "echo", IPC_ECHO, ipc_echo_receive, ipc_echo_cancel);
      shuso_configure_finish(S);
      shusoT_run_test(S, SHUTTLESOCK_MASTER, ipc_load_test, ipc_load_test_verify, ipc_check);
      assert_shuso_ran_ok(S);
    }
    
    test("buffer fill (500:1)") {
      ipc_check->received_stop_at = 1000;
      ipc_check->ping.procnum = SHUTTLESOCK_MANAGER;
      ipc_check->pong.procnum = SHUTTLESOCK_MASTER;
      ipc_check->ping.barrage = 400;
      ipc_check->pong.barrage = 1;
      ipc_check->ping.init_sleep_flag = 1;

      shuso_ipc_add_handler(S, "echo", IPC_ECHO, ipc_echo_receive, ipc_echo_cancel);
      shuso_configure_finish(S);
      shusoT_run_test(S, SHUTTLESOCK_MASTER, ipc_load_test, ipc_load_test_verify, ipc_check);
      assert_shuso_ran_ok(S);
    }
  }
}
#define MEM_DEFINED(addr) \
 (VALGRIND_CHECK_MEM_IS_ADDRESSABLE(addr) == 0 && VALGRIND_CHECK_MEM_IS_DEFINED(addr) == 0)

describe(stack_allocator) {
  static shuso_stalloc_t st;
  before_each() {
    shuso_system_initialize();
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
#ifndef SHUTTLESOCK_DEBUG_STALLOC_NOPOOL
    asserteq(st.allocd.last, NULL, "nothing should have been mallocd");
#endif
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
    size_t sz;
    sz = st.page.size / 5;
    if(sz == 0) sz = 10;
    
    for(int i=0; i<500; i++) {
      chr[i] = shuso_stalloc(&st, sz);
      memset(chr[i], 0x12, sz);
      for(int j=0; j<i; j++) {
        assertneq((void *)chr[i], (void *)chr[j], "those should be different allocations");
      }
#ifndef SHUTTLESOCK_DEBUG_STALLOC_NOPOOL
      asserteq(st.allocd.last, NULL, "nothing should have been mallocd");
#endif
    }
#ifndef SHUTTLESOCK_DEBUG_STALLOC_NOPOOL
    assert(st.page.count>1, "should have more than 1 page");
#else
    assert(st.page.count == 0, "should have 0 pages in no-pool mode");
#endif
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

void resolver_test(shuso_t *S, void *pd) {
  if(S->procnum != SHUTTLESOCK_MANAGER) {
    return;
  }
  shuso_resolve_hostname(&S->resolver, "google.com", AF_INET, resolve_check_ok, S);
}


describe(resolver) {
  static shuso_t *S = NULL;
  static test_runcheck_t  *chk = NULL;
  before_each() {
    S = shusoT_create(&chk, 25.0);
  }
  after_each() {
    shusoT_destroy(S, &chk);
  }
  
  test("resolve using system") {
    shuso_configure_finish(S);
    shusoT_run_test(S, SHUTTLESOCK_MANAGER, resolver_test, NULL, NULL);
    assert_shuso_ran_ok(S);
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
  static test_runcheck_t  *chk = NULL;
  before_each() {
    S = shusoT_create(&chk, 25.0);
  }
  after_each() {
    shusoT_destroy(S, &chk);
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

void listener_port_test(shuso_t *S, void *pd) {;
  listener_port_test_t *pt = S->data;
  assert(S->procnum == SHUTTLESOCK_MANAGER);
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
  static test_runcheck_t  *chk = NULL;
  before_each() {
    S = shusoT_create(&chk, 25.0);
    pt = shmalloc(pt);
    pt->err = NULL;
    S->data = pt;
  }
  after_each() {
    shusoT_destroy(S, &chk);
    shmfree(pt);
  }
  test("listen on port 34241") {
    pt->port = 34241;
    shuso_configure_finish(S);
    shusoT_run_test(S, SHUTTLESOCK_MANAGER, listener_port_test, NULL, pt);
    assert_shuso_ran_ok(S);
    if(pt->err) {
      snow_fail("error: %s", pt->err);
    }
  }
}

snow_main_decls;
int main(int argc, char **argv) {
  _snow.ignore_unknown_options = 1;
  memset(&test_config, 0x0, sizeof(test_config));
  dev_null = open("/dev/null", O_WRONLY);
  if(!set_test_options(&argc, argv)) {
    return 1;
  }
  int rc = snow_main_function(argc, argv);
  //printf("%i says bye with status code %i!\n\n", getpid(), rc);
  close(dev_null);
  fclose(stdin);
  fclose(stdout);
  fclose(stderr); 
  
  return rc;
}

#endif //__clang_analyzer__
