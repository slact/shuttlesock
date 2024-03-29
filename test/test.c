#include "test.h"
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <shuttlesock/modules/lua_bridge/api/ipc_lua_api.h>
#ifndef __clang_analyzer__

#define ONELINER(...) #__VA_ARGS__

test_config_t test_config;
int dev_null;

bool set_test_options(int *argc, char **argv) {
  snow_set_extra_help(""
    "    --verbose              Verbose test output.\n"
    "    --data-path=[path]     Path to test data directory.\n"
    "    --multiplier=[number]  Scale looping tests by this number.\n"
    "    --workers=[number]     Number of workers (defaults to number of CPU cores)\n"
  );
  int i = 1;
  while(i < *argc) {
    char *arg = argv[i];
    if(strcmp(arg, "--verbose") == 0) {
      test_config.verbose = true;
    }
    if((strstr(arg, "--data-path=") - arg) == 0) {
      test_config.data_path=&arg[strlen("--data-path=")];
    }
    if((strstr(arg, "--multiplier=") - arg) == 0) {
      test_config.multiplier=atof(&arg[strlen("--multiplier=")]);
    }
    if((strstr(arg, "--workers=") - arg) == 0) {
      test_config.workers=atoi(&arg[strlen("--workers=")]);
    }
    i++;
  }
  return true;
}

static void stop_shuttlesock(shuso_t *S, void *pd) {
  intptr_t procnum = (intptr_t)pd;
  assert(S->procnum == procnum);
  shuso_stop(S, SHUSO_STOP_ASK);
  
}

#define assert_conftest_ok(S, confstring) \
  luaL_checkstack(S->lua.state, 10, "no stack space"); \
  lua_pushstring(S->lua.state, confstring); \
  assert_luaL_dofile_args(S->lua.state, "test_config.lua", 2); \
  assert_shuso(S, )

#define assert_conftest_fail(S, confstring, error_match) \
  luaL_checkstack(S->lua.state, 10, "no stack space"); \
  lua_pushstring(S->lua.state, confstring); \
  assert_luaL_dofile_args(S->lua.state, "test_config.lua", 2); \
  assert_shuso_error(S, error_match)
  

describe(configuration) {
  static shuso_t          *S = NULL;
  static test_runcheck_t  *chk = NULL;
  static lua_State        *L = NULL;
  before_each() {
    S = shusoT_create(&chk, 5555.0);
    L = S->lua.state;
    assert_luaL_dofile(S->lua.state, "config_test_module.lua");
  }
  after_each() {
    if(S) shusoT_destroy(S, &chk);
  }
  
  subdesc(setting_paths) {
    test("generic path matching") {
      assert_luaL_dofile(L, "config_path_matching.lua");
    }
    test("unmatched path") {
      assert_conftest_fail(S, 
        "block2 { foo val; }",
        "unknown setting foo"
      );
    }
    test("matched path") {
      assert_conftest_ok(S, 
        "block3 { foo val; }"
      );
    }
  }
  
  subdesc(blocks) {
    test("missing block") {
      assert_conftest_fail(S, 
        "root_config \"foo\";",
        "\"root_config\".+ missing block"
      );
    }
    test("unexpected block") {
      assert_conftest_fail(S, 
        "bar \"foo\" {}",
        "\"bar\".+ unexpected block"
      );
    }
    test("semicolon after unexpected block") {
      assert_conftest_fail(S, 
        "bar \"foo\" {};",
        "unexpected \";\""
      );
    }
    test("semicolon after expected block") {
      assert_conftest_fail(S, 
        "root_config \"foo\" {};",
        "unexpected \";\""
      );
    }
    test("optional block present") {
      assert_conftest_ok(S, 
        "block_maybe {};"
      );
    }
    test("optional block absent") {
      assert_conftest_ok(S, 
        "block_maybe;"
      );
    }
  }
  
  subdesc(heredocs) {
    test("many heredocs on the same line") {
      assert_luaL_dofile_args(L, "test_config_heredocs_valid.lua", 1);
    }
    test("unterminated heredoc with no body") {
      assert_conftest_fail(S, 
        "bar <<~HEY_HEREDOC;",
        "unexpected end .* \"HEY_HEREDOC\""
      );
    }
    test("unterminated heredoc with body") {
      assert_conftest_fail(S, 
        "bar <<~HEY_HEREDOC;\nwhat\nohno",
        "unterminated heredoc.* HEY_HEREDOC"
      );
    }
  }
  
  subdesc(strings) {
    test("string values") {
      assert_luaL_dofile_args(L, "test_config_string_values.lua", 1);
    }
    test("unterminated string due to EOF") {
      assert_conftest_fail(S, 
        "bar \"oh look string",
        "unterminated string"
      );
    }
    test("unterminated string with stuff after it") {
      assert_conftest_fail(S, 
        "bar \"oh look string;\nbar hey;",
        "unterminated string"
      );
    }
  }
  
  subdesc(value_types) {
    test("all possible value types") {
      assert_luaL_dofile_args(L, "test_config_value_types.lua", 1);
    }
  }
  
  subdesc(defaults) {
    test("default value is present") {
      assert_luaL_dofile_args(L, "test_config_default_values.lua", 1);
    }
  }
  
  subdesc(variables) {
    subdesc(lua_bad_declaration) {
      test("variable isn't a table") {
        assert_luaL_dostring(L, "return {var=22}, 'variable .* must be a table'");
        assert_luaL_dofile_args(L, "test_config_module_bad_variable_declarations.lua", 3);
      }
      test("variable with a metatable") {
        assert_luaL_dostring(L, "return {var=setmetatable({name='foo'},{})}, 'variable $foo .*isn\\'t supposed to have a metatable'");
        assert_luaL_dofile_args(L, "test_config_module_bad_variable_declarations.lua", 3);
      }
      test("variable with no name") {
        assert_luaL_dostring(L, "return {'name'}, 'variable name is missing'");
        assert_luaL_dofile_args(L, "test_config_module_bad_variable_declarations.lua", 3);
      }
      test("variable with non-string name") {
        assert_luaL_dostring(L, "return {name={}}, 'variable name must be a string'");
        assert_luaL_dofile_args(L, "test_config_module_bad_variable_declarations.lua", 3);
      }
      test("variable with invalid aliases") {
        assert_luaL_dostring(L, "return {aliases=25}, 'variable $testvar aliases.* must be a table or string'");
        assert_luaL_dofile_args(L, "test_config_module_bad_variable_declarations.lua", 3);
      }
      test("variable with no path") {
        assert_luaL_dostring(L, "return {'path'}, 'variable $testvar path is missing'");
        assert_luaL_dofile_args(L, "test_config_module_bad_variable_declarations.lua", 3);
      }
      test("variable with non-string path") {
        assert_luaL_dostring(L, "return {path={}}, 'variable $testvar path must be a string'");
        assert_luaL_dofile_args(L, "test_config_module_bad_variable_declarations.lua", 3);
      }
      test("variable with no eval") {
        assert_luaL_dostring(L, "return {'eval'}, 'variable $testvar eval is missing'");
        assert_luaL_dofile_args(L, "test_config_module_bad_variable_declarations.lua", 3);
      }
      test("variable with non-function eval") {
        assert_luaL_dostring(L, "return {eval=coroutine.create(function()end)}, 'variable $testvar eval must be a function'");
        assert_luaL_dofile_args(L, "test_config_module_bad_variable_declarations.lua", 3);
      }
    }
    test("set $variable") {
      assert_luaL_dofile_args(L, "config_variable_simple.lua", 1);
    }
    test("constant module $variable") {
      assert_luaL_dofile_args(L, "test_config_module_variables.lua", 1);
    }
    subdesc(at_runtime) {
      test("cacheable variable on different workers") {
        assert_luaL_dofile_args(L, "test_config_module_variables_once_per_worker.lua", 1);
        shuso_run(S);
        assert_shuso_ran_ok(S);
      }
    }
  }
  
  test("module with a bunch of config settings") {
    assert_luaL_dofile_args(S->lua.state, "test_config_general.lua", 1);
    shuso_run(S);
    assert_shuso_ran_ok(S);
  }
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
    if(lua_gettop(S->lua.state) != 0) {
      luaS_printstack(S->lua.state, "too much stuff on the stack");
      assert(lua_gettop(S->lua.state) == 0);
    }
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
      S = shuso_createst();
      
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
      assert_shuso_error(S, "invalid subscribe event name \".+\"");
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
      S = shuso_createst();
    }
    after_each() {
      if(S) shuso_destroy(S);
    }
    test("bare-bones module") {
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
      assert_shuso(S, shuso_configure_finish(S));
      
      assert_luaL_dostring(L, "require 'shuttlesock.module'.find('luatest')");
    }
  }
}

int luaS_test_userdata_gxcopy_save(lua_State *L) {
  int *ud = (void *)lua_topointer(L, 1);
  lua_pushlightuserdata(L, ud);
  return 1;
}
int luaS_test_userdata_gxcopy_load(lua_State *L) {
  int *ud_in = (void *)lua_topointer(L, 1);
  assert(ud_in != NULL);
  int *ud = lua_newuserdata(L, sizeof(*ud));
  assert(ud != NULL);
  luaL_setmetatable(L, "test_userdata");
  *ud = *ud_in;
  return 1;
}

describe(lua_bridge) {
  subdesc(gxcopy) {
    static shuso_t   *Ss;
    static shuso_t   *Sd;
    static lua_State *Ls;
    static lua_State *Ld;
    before_each() {
      Ss = shuso_createst();
      Sd = shuso_createst();
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
    
    test("metatable referencing its table") {
       assert_luaL_dostring(Ls,"\
        local t={11,22,33,44,55} \
        local mt = {t=t} \
        setmetatable(t, mt) \
        return t"
      );
      assert(luaS_gxcopy_start(Ls, Ld));
      assert(luaS_gxcopy(Ls, Ld));
      assert(luaS_gxcopy_finish(Ls, Ld));
      shuso_destroy(Ss);
      Ss = NULL;
      lua_setglobal(Ld, "copy");
      assert_luaL_dostring(Ld, "\
        assert(copy[1] == 11)\n \
        assert(copy[2] == 22)\n \
        local mt = assert(getmetatable(copy))\n \
        assert(mt.t == copy)\n \
      ");
    }
    
    test("userdata") {
      luaL_Reg mt_fn[] = {
        {"__gxcopy_save", luaS_test_userdata_gxcopy_save},
        {"__gxcopy_load", luaS_test_userdata_gxcopy_load},
        {NULL, NULL}
      };
      luaL_newmetatable(Ls, "test_userdata");
      lua_pushinteger(Ls, 99);
      lua_setfield(Ls, -2, "number");
      
      luaL_newmetatable(Ld, "test_userdata");
      lua_pushinteger(Ld, 98);
      lua_setfield(Ld, -2, "number");
      
      luaL_setfuncs(Ls, mt_fn, 0);
      int *ud = lua_newuserdata(Ls, sizeof(*ud));
      assert(ud != NULL);
      *ud=27;
      luaL_setmetatable(Ls, "test_userdata");
      assert(luaS_gxcopy_start(Ls, Ld));
      assert(luaS_gxcopy(Ls, Ld));
      assert(luaS_gxcopy_finish(Ls, Ld));
      int *copied_ud = (void *)lua_topointer(Ld, -1);
      assert(copied_ud != NULL);
      assert(copied_ud != ud);
      assert(*copied_ud == *ud);
      lua_setglobal(Ld, "copy");
      assert_luaL_dostring(Ld, "\
        assert(type(copy) == 'userdata')\n \
        local mt = assert(getmetatable(copy))\n \
        assert(mt.number == 98)\n \
      ");
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
    
    test("gxcopy_check") {
      lua_State *L = Ss->lua.state;
      lua_newuserdata(L, sizeof(int));
      lua_pushlightuserdata(L, NULL);
      assert_luaL_dofile_args(L, "gxcopy_check.lua", 2);
    }
  }
  subdesc(printstack) {
    static shuso_t   *S;
    static lua_State *L; 
    before_each() {
      S = shuso_createst();
      L = S->lua.state;
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

describe(lua_features) {
  static lua_State *L;
  before_each() {
    L = luaL_newstate();
    luaL_openlibs(L);
  }
  after_each() {
    if(L) {
      lua_close(L);
    }
    L = NULL;
  }
  test("__gc on tables") {
    assert_luaL_dofile(L, "lua_feature___gc_on_tables.lua");
  }
}

describe(lua_api) {
  static shuso_t          *S = NULL;
  static test_runcheck_t  *chk = NULL;
  before_each() {
    S = shusoT_create(&chk, 5555.0);
  }
  after_each() {
    if(S) shusoT_destroy(S, &chk);
  }
  
  subdesc(utils) {
    test("streq") {
      lua_State *L = S->lua.state;
      lua_pushliteral(L, "beep");
      lua_pushliteral(L, "bamp");
      assert(!luaS_streq(L, -1, "fizz"));
      assert(!luaS_streq(L, -2, "buzz"));
      assert(luaS_streq(L, -1, "bamp"));
      assert(luaS_streq(L, -2, "beep"));
      
      assert(!luaS_streq_literal(L, -1, "fizz"));
      assert(!luaS_streq_literal(L, -2, "buzz"));
      assert(luaS_streq_literal(L, -1, "bamp"));
      assert(luaS_streq_literal(L, -2, "beep"));
    }
  }
  
  subdesc(modules) {
    test("a module") {
      lua_State *L = S->lua.state;
      assert_luaL_dofile(L, "module_simple.lua");
      assert_shuso(S, shuso_configure_finish(S));
      shuso_run(S);
      assert_shuso_ran_ok(S);
    }
    
    test("a module that can't be gxcopied") {
      lua_State *L = S->lua.state;
      assert_luaL_dofile( L, "module_with_bad_gxcopy.lua");
      shuso_configure_finish(S);
      assert_shuso_error(S, "failed to configure.*failed to initialize module.* contains a coroutine");
    }

    test("module publishing events") {
      assert_luaL_dofile(S->lua.state, "module_publishing_events.lua");
      assert_shuso(S, shuso_configure_finish(S));
      shuso_run(S);
      assert_shuso_ran_ok(S);
    }
    
    test("module subscribing to optional events") {
      assert_luaL_dofile(S->lua.state, "module_with_optional_events.lua");
      assert_shuso(S, shuso_configure_finish(S));
      shuso_run(S);
      assert_shuso_ran_ok(S);
    }
  }
  
  subdesc(listen) {
    test("module with listening blocks") {
      assert_luaL_dofile(S->lua.state, "module_config_listen.lua");
      assert_shuso(S, shuso_configure_finish(S));
      shuso_run(S);
      assert_shuso_ran_ok(S);
    }
  }
  
  test("lazy atomics") {
    lua_State *L = S->lua.state;
    assert_luaL_dofile(L, "lazy_atomics.lua");
    assert_shuso(S, shuso_configure_finish(S));
    shuso_run(S);
    assert_shuso_ran_ok(S);
  }
  
  test("core:error events") {
    assert_luaL_dofile(S->lua.state, "core_error_events.lua");
    assert_shuso(S, shuso_configure_finish(S));
    chk->ignore_errors = 1;
    shuso_run(S);
    assert_shuso_ran_ok(S);
  }
  
  test("version string") {
    lua_State *L = S->lua.state;
    lua_pushstring(L, SHUTTLESOCK_VERSION_STRING);
    lua_pushinteger(L, SHUTTLESOCK_VERSION_MAJOR);
    lua_pushinteger(L, SHUTTLESOCK_VERSION_MINOR);
    lua_pushinteger(L, SHUTTLESOCK_VERSION_PATCH);
    lua_pushinteger(L, SHUTTLESOCK_VERSION);
    assert_luaL_dofile_args(L, "check_shuttlesock_version.lua", 5);
    assert_shuso(S, shuso_configure_finish(S));
    shuso_run(S);
    assert_shuso_ran_ok(S);
  }
  
  subdesc(ipc) {
    test("pack/unpack data in heap") {
      assert_shuso(S, shuso_configure_finish(S));
      lua_State *L = S->lua.state;
      
      assert_luaL_dostring(L,ONELINER(
        local x = ({});
        x.x=x;
        local self = {
          121,
          x,
          {
            112,
            "banana",
            true,
            false,
            x=12,
          },
          {[{}]='!!!'}
        };
        self.self = self;
        return self;
      ));
      
      shuso_ipc_lua_data_t *data = luaS_lua_ipc_pack_data(L, -1, "beep", false);
      lua_pop(L, 1);
      lua_gc(L, LUA_GCCOLLECT, 0);
      luaS_lua_ipc_unpack_data(L, data);
      lua_setglobal(L, "unpacked");
      
      int reftable_index = data->reftable;
      lua_rawgeti(L, LUA_REGISTRYINDEX, reftable_index);
      lua_setglobal(L, "data_reftable");
      
      lua_pushnumber(L, reftable_index);
      lua_setglobal(L, "reftable_index");
      
      shuso_log_warning(S, "hey!");
      
      assert_luaL_dostring(L, ONELINER(
        assert(type(unpacked) == "table") \n
        assert(unpacked.self == unpacked) \n
        assert(unpacked[1]==121)\n
        assert(unpacked[2].x == unpacked[2]) \n
        assert(unpacked[3][1]==112)\n
        assert(unpacked[3][2]=="banana")\n
        assert(unpacked[3][3]==true) \n
        local k, v = next(unpacked[4], nil) \n
        assert(type(k)=="table" and next(k) == nil) \n
        assert(v == "!!!") \n
        _G.weak=setmetatable({data=unpacked}, {__mode="v"}) \n
        local n = 0 \n
        for k,v in pairs(_G.data_reftable) do \n
          if type(v) ~= "string" and type(v) ~= "number" then \n
            _G.weak[k]=v \n
            n=n+1 \n
          end\n
        end \n
        _G.data_reftable=nil\n
        assert(n>1)\n
        local reftable = debug.getregistry()[_G.reftable_index] \n
        _G.reftable_signature = tostring(reftable) \n
        
        assert(tostring(debug.getregistry()[_G.reftable_index]) == _G.reftable_signature) \n
        assert(debug.getregistry()[_G.reftable_index] ~= nil, "reftable's still there") \n
      ));
      
      assert_luaL_dostring(L, "_G.weak.data = nil");
      //ok it should be gone now
      lua_gc(L, LUA_GCCOLLECT, 0);
      
      assert_luaL_dostring(L, ONELINER(
        local count = 0 \n
        for k,v in pairs(_G.weak) do \n
          count = count + 1 \n
        end \n
        assert(count > 1, "table contents persist. good") \n
      ));
      
      luaS_lua_ipc_gc_data(L, data);
      //ok all the refs keeping this data around should be gone now
      lua_gc(L, LUA_GCCOLLECT, 0);
      
      assert_luaL_dostring(L, ONELINER(
        local count = 0 \n
        for k,v in pairs(_G.weak) do \n
          count = count + 1\n
        end \n
        assert(count == 0, "table contents should have been cleared") \n
        
        assert(tostring(debug.getregistry()[_G.reftable_index]) ~= _G.reftable_signature, "reftable's gone");
      ));
      
      //reftable_ref
      
    }
    
    test("pack/unpack data in shared memory") {
      assert_shuso(S, shuso_configure_finish(S));
      lua_State *L = S->lua.state;
      assert_luaL_dostring(L,"return {'hello','hello',10,{foo='bar',20,{{}}}}");
      shuso_ipc_lua_data_t *data = luaS_lua_ipc_pack_data(L, -1, "beep", true);
      lua_gc(L, LUA_GCCOLLECT, 0);
      
      luaS_lua_ipc_unpack_data(L, data);
      lua_setglobal(L, "unpacked");
      
      assert_luaL_dostring(L, " \
        assert(type(unpacked) == 'table')\n\
        assert(unpacked[1] == 'hello')\n\
        assert(unpacked[2] == 'hello')\n\
        assert(unpacked[3] == 10)\n\
        assert(unpacked[4]['foo'] == 'bar')\n\
        assert(unpacked[4][1]==20)\n\
        assert(type(unpacked[4][2]) == 'table')\n\
        assert(type(unpacked[4][2][1]) == 'table')\n\
      ");
      
      luaS_lua_ipc_gc_data(L, data);
      //TODO: test that shared memory gets actually released
    }
    
    test("single round-trip") {
      assert_luaL_dofile(S->lua.state, "ipc_single_roundtrip.lua");
      assert_shuso(S, shuso_configure_finish(S));
      shuso_run(S);
      assert_shuso_ran_ok(S);
    }
    
    test("master-manager communication") {
      assert_luaL_dofile(S->lua.state, "ipc_master_manager.lua");
      assert_shuso(S, shuso_configure_finish(S));
      shuso_run(S);
      assert_shuso_ran_ok(S);
    }
    test("master-worker communication") {
      assert_luaL_dofile(S->lua.state, "ipc_master_worker.lua");
      assert_shuso(S, shuso_configure_finish(S));
      shuso_run(S);
      assert_shuso_ran_ok(S);
    }
    
    test("buffer fill") {
      assert_luaL_dofile(S->lua.state, "ipc_fill_buffer.lua");
      assert_shuso(S, shuso_configure_finish(S));
      shuso_run(S);
      assert_shuso_ran_ok(S);
    }
    
    test("broadcasting") {
      lua_pushstring(S->lua.state, "all");
      lua_pushinteger(S->lua.state, 5000 * test_config.multiplier);
      assert_luaL_dofile_args(S->lua.state, "ipc_broadcast.lua", 2);
      assert_shuso(S, shuso_configure_finish(S));
      shuso_run(S);
      assert_shuso_ran_ok(S);
    }
  }
}

#define IPC_ECHO 130

typedef struct {
  int           active;
  int           procnum;
  int           barrage;
  int           barrage_received;
  _Atomic(int)  seq;
  _Atomic(int)  seq_received;
  _Atomic(bool) init_sleep_flag;
  float         sleep;
  float         slept;
  
} ipc_check_oneway_t;

typedef struct {
  _Atomic(bool)       failure;
  char                err[1024];
  _Atomic(int)        sent;
  _Atomic(int)        received;
  _Atomic(int)        prev_received;
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


#define check_ipc(test,S,chk,...) \
do { \
  if(!(test)) { \
    if(!chk->failure) { \
      sprintf(chk->err, __VA_ARGS__); \
    }\
    chk->failure = true;\
    shuso_log_warning(S, __VA_ARGS__); \
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
    shuso_log_debug(S, "chk->sent: %d, received_stop_at: %d", chk->sent, chk->received_stop_at);
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
  atomic_fetch_add(&chk->received, 1);
  atomic_fetch_add(&self->seq_received, 1);
  shuso_log(S, "received %d", self->seq_received);
  
  int sent = chk->sent, received = chk->received, self_seq_received = self->seq_received;
  
  check_ipc(received <= sent, S, chk, "sent - received mismatch for procnum %d: send %d > received %d", self->procnum, sent, received);
  check_ipc(seq == self_seq_received, S, chk, "seq mismatch for procnum %d: expected %d, got %ld", self->procnum, self_seq_received, seq);
  
  if(chk->received >= chk->received_stop_at) {
    //we're done;
    shuso_log(S, "it's time to stop");
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
  if(chk->failure) {
    snow_fail("%s", chk->err);
  }
}
#undef check_ipc

void ipc_echo_cancel(shuso_t *S, const uint8_t code, void *ptr) { }

describe(ipc) {
  static shuso_t          *S = NULL;
  static test_runcheck_t  *chk = NULL;
  
  subdesc(one_to_one) {
    static ipc_check_t *ipc_check = NULL;
    
    before_each() {
      S = shusoT_create(&chk, 100.0);
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
    
    test("buffer fill (1000:1)") {
      ipc_check->received_stop_at = 5000;
      ipc_check->ping.procnum = SHUTTLESOCK_MANAGER;
      ipc_check->pong.procnum = SHUTTLESOCK_MASTER;
      ipc_check->ping.barrage = 1000;
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

describe(pool_allocator) {
  static shuso_pool_t pool;
  before_each() {
    shuso_system_initialize();
    shuso_pool_init(&pool, 0);
  }
  after_each() {
    shuso_pool_empty(&pool);
  }
  
  test("page header alignment") {
    asserteq(sizeof(shuso_pool_page_t) % sizeof(void *), 0, "page header struct must be native pointer size aligned");
  }
  test("handful of pointer-size allocs") {
    void *ptr[10];
    for(int i=0; i<10; i++) {
      ptr[i] = shuso_palloc(&pool, sizeof(void *));
      ptr[i] = (void *)(intptr_t)i;
      ptr[i] = &((char *)&ptr[i])[1];
      for(int j=0; j<i; j++) {
        assertneq(ptr[i], ptr[j], "those should be different allocations");
      }
    }
#ifndef SHUTTLESOCK_DEBUG_NOPOOL
    asserteq(pool.allocd.last, NULL, "nothing should have been mallocd");
#endif
  }
  
  test("some very large allocs") {
    char *chr[10];
    size_t sz = pool.page.size + 10;
    for(int i=0; i<10; i++) {
      chr[i] = shuso_palloc(&pool, sz);
      memset(chr[i], 0x12, sz);
      for(int j=0; j<i; j++) {
        assertneq((void *)chr[i], (void *)chr[j], "those should be different allocations");
      }
      assertneq(pool.allocd.last, NULL, "alloc shouldn't be NULL");
      asserteq((void *)pool.allocd.last->data, (void *)chr[i], "wrong last alloc");
    }
  }
  
  test("a few pages' worth") {
    static char *chr[500];
    size_t sz;
    sz = pool.page.size / 5;
    if(sz == 0) sz = 10;
    
    for(int i=0; i<500; i++) {
      chr[i] = shuso_palloc(&pool, sz);
      memset(chr[i], 0x12, sz);
      for(int j=0; j<i; j++) {
        assertneq((void *)chr[i], (void *)chr[j], "those should be different allocations");
      }
#ifndef SHUTTLESOCK_DEBUG_NOPOOL
      asserteq(pool.allocd.last, NULL, "nothing should have been mallocd");
#endif
    }
#ifndef SHUTTLESOCK_DEBUG_NOPOOL
    assert(pool.page.count>1, "should have more than 1 page");
#else
    assert(pool.page.count == 0, "should have 0 pages in no-pool mode");
#endif
  }
  subdesc(levels) {
    static test_pool_stats_t stats;
    before_each() {
      shuso_pool_init(&pool, 0);
      memset(&stats, 0x00, sizeof(stats));
    }
    after_each() {
      shuso_pool_empty(&pool);
    }
    
    
    test("mark") {
      fill_pool(&pool, &stats, 1, 256, 30, 1000, 8);
    }
    
    test("mark/drain") {
      fill_pool(&pool, &stats, 1, 256, 30, 1000, 8);
      shuso_pool_drain_to_level(&pool, 3);
      assert(pool.allocd.last == stats.levels.array[2].allocd);
      assert(pool.page.last == stats.levels.array[2].page);
      assert(pool.page.cur == stats.levels.array[2].page_cur);
      shuso_pool_drain_to_level(&pool, 0);
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
    unsigned total_allocs = 5000 * test_config.multiplier;
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

/*
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
*/
/*
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
  skip("listen on port 34241") {
    pt->port = 34241;
    shuso_configure_finish(S);
    shusoT_run_test(S, SHUTTLESOCK_MANAGER, listener_port_test, NULL, pt);
    assert_shuso_ran_ok(S);
    if(pt->err) {
      snow_fail("error: %s", pt->err);
    }
  }
}
*/
snow_main_decls;
int main(int argc, char **argv) {
  _snow.ignore_unknown_options = 1;
  test_config = (test_config_t) {
    .verbose = false,
    .data_path="test/data",
    .multiplier = 1.0,
    .workers = 0,
  };
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
