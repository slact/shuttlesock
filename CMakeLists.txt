cmake_minimum_required(VERSION 3.4)
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake" )
include(SetProjectCompiler)

project(shuttlesock LANGUAGES "C" VERSION 0.0.1)
set(DESCRIPTION "speedy server library with a focus on moving sockets between processes")

include(CheckSymbolExists)
include(CMakePushCheckState)
include(TestBigEndian)
include(GNUInstallDirs)
include(MakeCmakeALittleLessTerribleForOurUseCase)
include(TargetRequirePackage)

#set(CMAKE_VERBOSE_MAKEFILE ON)
set(libsrc 
  src/shuttlesock.c
  src/watchers.c
  src/ipc.c
  src/ipc_commands.c
  src/sysutil.c
  src/stalloc.c
)

add_library(shuttlesock SHARED ${libsrc})
set_property(TARGET shuttlesock PROPERTY C_STANDARD 11)

target_include_directories(shuttlesock PUBLIC src/include)
target_include_directories(shuttlesock PUBLIC ${PROJECT_BINARY_DIR}/src/include)
target_include_directories(shuttlesock PRIVATE src)

#wahern's asynchrnous dns.c library
add_library(dns.c STATIC lib/dns/dns.c)
set_property(TARGET dns.c PROPERTY C_STANDARD 11)
target_include_directories(shuttlesock PRIVATE lib/dns)

#typeof() check
include(TestTypeofKeyword)
test_typeof_keyword(SHUTTLESOCK_HAVE_TYPEOF)

#ptrheads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(shuttlesock PUBLIC ${CMAKE_THREAD_LIBS_INIT})

target_require_package(shuttlesock PUBLIC ev)

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
target_include_directories(shuso_test PRIVATE test)

configure_file(src/include/shuttlesock/configure.h.tmpl src/include/shuttlesock/configure.h)
