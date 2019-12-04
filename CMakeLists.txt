cmake_minimum_required(VERSION 3.4)
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake" )
include(SetProjectCompiler)

project(shuttlesock LANGUAGES "C" VERSION 0.0.1)
set(DESCRIPTION "speedy server library with a focus on moving sockets between processes")
include(PaddedVersionNumber)
padded_version_number(shuttlesock_VERSION_PADDED shuttlesock)

include(CheckSymbolExists)
include(CMakePushCheckState)
include(TestBigEndian)
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
  src/stalloc.c
  src/resolver.c
  src/shared_slab.c
  src/module.c
  src/module_event.c
  src/embedded_lua_scripts.c
  src/lua_utils.c
)

target_include_directories(shuttlesock PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/src/include)
target_include_directories(shuttlesock PUBLIC src/include)
target_include_directories(shuttlesock PRIVATE src)

#typeof() check
include(TestTypeofKeyword)
test_typeof_keyword(SHUTTLESOCK_HAVE_TYPEOF)

# _Generic macro check
include(TestTypeGenericMacros)
test_type_generic_macros(SHUTTLESOCK_HAVE_TYPE_GENERIC_MACROS)
if(NOT SHUTTLESOCK_HAVE_TYPE_GENERIC_MACROS)
  message(FATAL_ERROR "Shuttlesock requires the compiler to support the _Generic macro. Yours doesn't.")
endif()

#pthreads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(shuttlesock PUBLIC ${CMAKE_THREAD_LIBS_INIT})
if(NOT LIBEV_STATIC)
  target_require_package(shuttlesock PUBLIC ev)
else()
  set(LIBEV_PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/libev)
  include(ExternalProject)
  ExternalProject_Add(libev_autoconf
    URL "http://dist.schmorp.de/libev/libev-4.27.tar.gz"
  #  SOURCE_DIR ${LIBEV_DIR}
    CONFIGURE_COMMAND /bin/sh -c "CFLAGS=\"${CFLAGS} -w\" ./configure --prefix=\"${LIBEV_PREFIX_DIR}\" --enable-shared=no --with-pic=yes"
    PREFIX ${LIBEV_PREFIX_DIR}
    BUILD_COMMAND make
    INSTALL_COMMAND make install
    BUILD_IN_SOURCE 1
  )
  target_include_directories(shuttlesock PUBLIC ${LIBEV_PREFIX_DIR}/include)
  add_dependencies(shuttlesock libev_autoconf)
  add_library(libev STATIC IMPORTED)
  set_target_properties(libev PROPERTIES IMPORTED_LOCATION ${LIBEV_PREFIX_DIR}/lib/libev.a)
  target_link_libraries(shuttlesock PRIVATE libev)
endif()

test_big_endian(SHUTTLESOCK_BIG_ENDIAN)

cmake_push_check_state(RESET)
check_symbol_exists(eventfd "sys/eventfd.h" SHUTTLESOCK_HAVE_EVENTFD)
if(NOT SHUTTLESOCK_HAVE_EVENTFD)
  set(SHUTTLESOCK_USE_EVENTFD "" CACHE INTERNAL "use eventfd if present" FORCE)
else()
  set(SHUTTLESOCK_USE_EVENTFD "TRUE" CACHE INTERNAL "use eventfd if present")
endif()
cmake_reset_check_state()

include(TestPthreadSetname)
test_pthread_setname("${CMAKE_THREAD_LIBS_INIT}"
  SHUTTLESOCK_PTHREAD_SETNAME_STYLE
  SHUTTLESOCK_PTHREAD_SETNAME_INCLUDE_PTRHEAD_NP
)

#SO_REUSEPORT
include(TestSO_REUSEPORT)
test_SO_REUSEPORT(SHUTTLESOCK_HAVE_SO_REUSEPORT)
if(NOT SHUTTLESOCK_HAVE_SO_REUSEPORT)
  message(FATAL_ERROR "Shuttlesock requires SO_REUSEPORT support. Unfortunately this system doesn't have it.")
else()
  set(SHUTTLESOCK_HAVE_SO_REUSEPORT "TRUE" CACHE INTERNAL "Have SO_REUSEPORT on this system" FORCE)
endif()

#IPv6
include(TestIPv6)
test_ipv6(SHUTTLESOCK_HAVE_IPV6)
set(SHUTTLESOCK_HAVE_IPV6 ${SHUTTLESOCK_HAVE_IPV6} CACHE INTERNAL "system supports IPv6")

#strsignal()
include(TestStrsignal)
test_strsignal(SHUTTLESOCK_HAVE_STRSIGNAL)
set(SHUTTLESOCK_HAVE_STRSIGNAL ${SHUTTLESOCK_HAVE_STRSIGNAL} CACHE INTERNAL "system has strsignal()")

#lua (we want 5.3)
if(NOT LUA_BUILD_STATIC_APICHECK)
  set(Lua_FIND_VERSION 5.3)
  include(FindLua)
  if(LUA_INCLUDE_DIR)
    target_include_directories(shuttlesock PUBLIC ${LUA_INCLUDE_DIR})
    target_link_libraries(shuttlesock PUBLIC ${LUA_LIBRARIES})
  else()
    message(FATAL_ERROR "Failed to find Lua ${Lua_FIND_VERSION}")
  endif()
else()
  if(CMAKE_SYSTEM_NAME STREQUAL Linux)
    set(LUA_BUILD_TARGET linux)
  elseif(CMAKE_SYSTEM_NAME STREQUAL Darwin)
    set(LUA_BUILD_TARGET macosx)
  elseif(CMAKE_SYSTEM_NAME STREQUAL FreeBSD)
    set(LUA_BUILD_TARGET freebsd)
  elseif(CMAKE_SYSTEM_NAME STREQUAL Solaris)
    set(LUA_BUILD_TARGET solaris)
  elseif(CMAKE_SYSTEM_NAME STREQUAL BSD)
    set(LUA_BUILD_TARGET bsd)
  else()
    set(LUA_BUILD_TARGET posix)
  endif()
  
  
  
  set(LUA_PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/src/lua_static_apicheck/)
  include(ExternalProject)
  ExternalProject_Add(lua_static_apicheck
    URL "https://www.lua.org/ftp/lua-5.3.5.tar.gz"
  #  SOURCE_DIR ${LIBEV_DIR}
    CONFIGURE_COMMAND ""
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}
    BUILD_COMMAND make "MYCFLAGS=${CFLAGS} -DLUA_USE_APICHECK -fPIC -g -DLUA_COMPAT_5_2 -DLUA_COMPAT_5_1" ${LUA_BUILD_TARGET}
    INSTALL_COMMAND ""
    BUILD_IN_SOURCE 1
  )
  target_include_directories(shuttlesock PUBLIC ${LUA_PREFIX_DIR}/src)
  add_dependencies(shuttlesock lua_static_apicheck)
  add_library(lualib_apicheck STATIC IMPORTED)
  set_target_properties(lualib_apicheck PROPERTIES IMPORTED_LOCATION ${LUA_PREFIX_DIR}/src/liblua.a)
  target_link_libraries(shuttlesock PRIVATE lualib_apicheck)
  target_link_libraries(shuttlesock PRIVATE dl)
endif()

#c-ares
include(TestC-aresVersion)
#version 1.13.0 is where ares_set_socket_functions got added, which we need
if(NOT C_ARES_BUILD_STATIC)
  target_require_package(shuttlesock PUBLIC cares HEADER_NAME ares.h OPTIONAL C_ARES_FOUND)
  if(C_ARES_FOUND)
    test_c_ares_version_min(C_ARES_VERSION_OK 1 13 0)
  endif()
endif()
if(C_ARES_FOUND AND C_ARES_VERSION_OK)
  target_require_package(shuttlesock PUBLIC cares ares.h)
  target_link_libraries(shuttlesock PUBLIC cares)
else()
  set(CARES_STATIC ON CACHE BOOL "build c-ares as static lib" FORCE)
  set(CARES_SHARED OFF CACHE BOOL "build c-ares as shared lib" FORCE)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE INTERNAL "-fPIC" FORCE)
  message(STATUS "Build bundled version of c-ares")
  add_subdirectory(lib/c-ares EXCLUDE_FROM_ALL)
  target_include_directories(shuttlesock PUBLIC lib/c-ares)
  target_link_libraries(shuttlesock PUBLIC c-ares)
endif()

#Linux kernel's io_uring
include(TestIoUringHeaders)
if("${SHUTTLESOCK_USE_IO_URING}" OR NOT DEFINED SHUTTLESOCK_USE_IO_URING)
  target_include_directories(shuttlesock PRIVATE lib/liburing/src)
  test_io_uring_buildable(SHUTTLESOCK_CAN_BUILD_IO_URING)
  if(NOT "${SHUTTLESOCK_CAN_BUILD_IO_URING}")
    set(SHUTTLESOCK_USE_IO_URING OFF CACHE INTERNAL "try to use Linux Kernel's io_uring.")
  else()
    #liburing
    add_library(uring STATIC 
      lib/liburing/src/queue.c
      lib/liburing/src/register.c
      lib/liburing/src/setup.c
      lib/liburing/src/syscall.c
    )
    if("${CMAKE_C_COMPILER_ID}" MATCHES "^(GNU)|((Apple)?Clang)$")
      target_compile_options(uring PRIVATE -Wno-pointer-arith)
    endif()
    set(SHUTTLESOCK_USE_IO_URING ON CACHE INTERNAL "try to use Linux Kernel's io_uring. If we're not on linux, that's ok, we can still try and fail gracefully.")
    target_link_libraries(shuttlesock PRIVATE uring)
  endif()
endif()

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER  ${CCACHE_PROGRAM})
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

add_executable(shuso_test 
  test/test.c
  test/util.c
)
target_link_libraries(shuso_test PRIVATE shuttlesock)
target_include_directories(shuso_test PRIVATE ${LUA_INCLUDE_DIR})
target_link_libraries(shuso_test PRIVATE ${LUA_LIBRARIES})
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
pack_lua_module("shuttlesock.core.module_event"  src/module_event.lua)

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
    api/lua_ipc.c
    lua_binding.c
  HEADERS
    lua_bridge.h
    PRIVATE private.h
    PRIVATE api/lazy_atomics.h
    PRIVATE api/lua_ipc.h
  LUA_MODULES
    #API for Lua modules
    "shuttlesock"              api/shuttlesock.lua
    "shuttlesock.log"          api/log.lua
    "shuttlesock.module"       api/module.lua
    "shuttlesock.atomics"      api/atomics.lua
    "shuttlesock.watcher"      api/watcher.lua
    "shuttlesock.process"      api/process.lua
    "shuttlesock.config"       api/config.lua
    "shuttlesock.ipc"          api/ipc.lua
  LUA_SOURCES
    #utils used inside "shuttlesock.core" lua_binding
    "lua_binding" lua_binding.lua
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
