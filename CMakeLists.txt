cmake_minimum_required(VERSION 3.4)
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake" )

include(SetProjectCompiler)

project(shuttlesock LANGUAGES "C" VERSION 0.0.1)
set(DESCRIPTION "speedy server library with a focus on moving sockets between processes")

include(PaddedVersionNumber)
padded_version_number(shuttlesock_VERSION_PADDED shuttlesock)

include(GNUInstallDirs)
include(MakeCmakeALittleLessTerribleForOurUseCase)
include(TargetRequirePackage)
include(ShuttlesockModule)
#set(CMAKE_VERBOSE_MAKEFILE ON)

add_library(shuttlesock SHARED)
set_property(TARGET shuttlesock PROPERTY C_STANDARD 11)

target_sources(shuttlesock PRIVATE
  src/shuttlesock.c
  src/log.c
  src/watchers.c
  src/ipc.c
  src/ipc_commands.c
  src/sysutil.c
  src/pool.c
  src/resolver.c
  src/shared_slab.c
  src/module.c
  src/event.c
  src/embedded_lua_scripts.c
  src/lua_utils.c
  src/instring.c
  src/io.c
  src/buffer.c
)

target_include_directories(shuttlesock PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/src/include)
target_include_directories(shuttlesock PUBLIC src/include)
target_include_directories(shuttlesock PRIVATE src)

set(THIRDPARTY_DOWNLOAD "${CMAKE_CURRENT_LIST_DIR}/thirdparty")
set(THIRDPARTY_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/thirdparty")
file(MAKE_DIRECTORY "${THIRDPARTY_PREFIX}")
target_include_directories(shuttlesock SYSTEM PUBLIC "${THIRDPARTY_PREFIX}/include")

set(SHUTTLESOCK_USE_HYPERSCAN OFF) #no hyperscan for now

include(ShuttlesockTestFeatures)
shuttlesock_test_features(
  CONDITIONS
    USE_EVENTFD           SHUTTLESOCK_USE_EVENTFD
    USE_IO_URING          SHUTTLESOCK_USE_IO_URING
    USE_HYPERSCAN         SHUTTLESOCK_USE_HYPERSCAN
  RESULTS
    TYPEOF_MACRO          SHUTTLESOCK_HAVE_TYPEOF
    TYPE_GENERIC_MACROS   SHUTTLESOCK_HAVE_TYPE_GENERIC_MACROS
    BIG_ENDIAN            SHUTTLESOCK_BIG_ENDIAN
    EVENTFD               SHUTTLESOCK_HAVE_EVENTFD
    IO_URING              SHUTTLESOCK_HAVE_IO_URING
    PTHREAD_SETNAME       SHUTTLESOCK_PTHREAD_SETNAME_STYLE
    PTHREAD_NP_REQUIRED   SHUTTLESOCK_PTHREAD_SETNAME_INCLUDE_PTRHEAD_NP
    SO_REUSEPORT          SHUTTLESOCK_HAVE_SO_REUSEPORT
    IPV6                  SHUTTLESOCK_HAVE_IPV6
    ACCEPT4               SHUTTLESOCK_HAVE_ACCEPT4
    STRSIGNAL             SHUTTLESOCK_HAVE_STRSIGNAL
    HYPERSCAN             SHUTTLESOCK_HAVE_HYPERSCAN
)

#set default max number of workers
set(SHUTTLESOCK_MAX_WORKERS 256 CACHE INTERNAL "max number of workers")

#pthreads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(shuttlesock PUBLIC ${CMAKE_THREAD_LIBS_INIT})

#libev
include(ShuttlesockLibev)
shuttlesock_link_libev("${LIBEV_BUILD_STATIC}" "${LIBEV_BUILD_STATIC_EXTRAFLAGS}")

#Lua 5.4
include(ShuttlesockLua)
shuttlesock_link_lua("${LUA_VERSION}" "${LUA_BUILD_STATIC}" "${LUA_BUILD_STATIC_EXTRAFLAGS}")

#c-ares
include(ShuttlesockC-ares)
shuttlesock_link_c_ares("${C_ARES_BUILD_STATIC}")

#Linux kernel's io_uring
include(ShuttlesockLiburing)
if(SHUTTLESOCK_HAVE_IO_URING)
  shuttlesock_link_liburing("${LIBURING_BUILD_STATIC}")
endif()

#openssl
include(ShuttlesockOpenSSL)
shuttlesock_link_openssl("${OPENSSL_BUILD_STATIC}")

#pico http parser
include(ShuttlesockPicoHttpParser)
shuttlesock_link_picohttpparser()

#nghttp2
include(ShuttlesockNghttp2)
shuttlesock_link_nghttp2("${NGHTTP2_BUILD_STATIC}")

#pcre2
include(ShuttlesockPCRE)
shuttlesock_link_pcre("${PCRE_BUILD_STATIC}")

#hyperscan - x86-64 stupid fast kitchen sink multi-regex engine
# (but no backreferences)
include(ShuttlesockHyperscan)
if(SHUTTLESOCK_HAVE_HYPERSCAN)  
  shuttlesock_link_hyperscan("${HYPERSCAN_BUILD_STATIC}")
endif()

add_executable(shuso_test 
  test/test.c
  test/test_utils.c
)
target_link_libraries(shuso_test PRIVATE shuttlesock)
target_include_directories(shuso_test PRIVATE test)

#set default log level
set(SHUTTLESOCK_LOG_LEVELS "DEBUG" "INFO" "NOTICE" "WARNING" "ERROR" "CRITICAL" "FATAL")
set(SHUTTLESOCK_DEFAULT_LOGLEVEL "INFO" CACHE STRING "Default log level")
if(NOT (SHUTTLESOCK_DEFAULT_LOGLEVEL IN_LIST SHUTTLESOCK_LOG_LEVELS))
  message(FATAL_ERROR "SHUTTLESOCK_DEFAULT_LOGLEVEL invalid value ${SHUTTLESOCK_DEFAULT_LOGLEVEL}, must be one of: ${SHUTTLESOCK_LOG_LEVELS}")
endif()

include(PackLuaScripts)

#some luarocks we use
pack_lua_bundled_module("argparse" src/luarocks/argparse/src/argparse.lua)
pack_lua_bundled_module("mm"       src/luarocks/mm.lua)
pack_lua_bundled_module("inspect"  src/luarocks/inspect.lua)

#modules used by the shuttlesock core
pack_lua_module("shuttlesock.core.module"        src/module.lua)
pack_lua_module("shuttlesock.core.event"         src/event.lua)
pack_lua_module("shuttlesock.core.instring"      src/instring.lua)

add_core_module("core"
  SOURCES core.c
  HEADERS core.h
)

add_core_module("config"
  SOURCES config.c
  LUA_MODULES
    "shuttlesock.core.config" config.lua
  HEADERS
    config.h
    PRIVATE private.h
)

add_core_module("lua_bridge"
  SOURCES
    lua_bridge.c
    api/lazy_atomics.c
    api/ipc_lua_api.c
    api/io_lua.c
    api/core_lua_api.c
  HEADERS
    lua_bridge.h
    PRIVATE private.h
    PRIVATE api/lazy_atomics.h
    PRIVATE api/ipc_lua_api.h
    PRIVATE api/io_lua.h
  LUA_MODULES
    #API for Lua modules
    "shuttlesock"                 api/shuttlesock.lua
    "shuttlesock.log"             api/log.lua
    "shuttlesock.module"          api/module.lua
    "shuttlesock.event"           api/event.lua
    "shuttlesock.atomics"         api/atomics.lua
    "shuttlesock.watcher"         api/watcher.lua
    "shuttlesock.process"         api/process.lua
    "shuttlesock.config"          api/config.lua
    "shuttlesock.coroutine"       api/coroutine.lua
    "shuttlesock.ipc"             api/ipc.lua
    "shuttlesock.io"              api/io.lua
    "shuttlesock.utils"           api/utils.lua
    "shuttlesock.debug"           api/debug.lua
    "shuttlesock.debug.spinlock"  api/debug/spinlock.lua
    "shuttlesock.debug.mutex"     api/debug/mutex.lua
  LUA_SOURCES
    #utils used inside "shuttlesock.core" lua_binding
    "shuttlesock.core" api/core.lua
)

add_core_module("server" LUA
  LUA_MODULES
    "shuttlesock.modules.core.server" server.lua
  SOURCES
    server.c
  HEADERS
    server.h
  PREPARE_FUNCTION
    shuttlesock_server_module_prepare
  LUA_REQUIRE
    shuttlesock.modules.core.server
)

set_lua_packed_script_file("src/embedded_lua_scripts.c")
finish_adding_core_modules()
configure_file(src/include/shuttlesock/build_config.h.tmpl src/include/shuttlesock/build_config.h)
