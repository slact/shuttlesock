cmake_minimum_required(VERSION 3.4)
set(CMAKE_MODULE_PATH "./cmake")
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
)

add_library(shuttlesock SHARED ${libsrc})
target_compile_features(shuttlesock PRIVATE c_std_11 c_static_assert c_variadic_macros)

target_include_directories(shuttlesock PUBLIC src/include)
target_include_directories(shuttlesock PUBLIC ${PROJECT_BINARY_DIR}/src/include)
target_include_directories(shuttlesock PRIVATE src)

#ptrheads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(shuttlesock PUBLIC ${CMAKE_THREAD_LIBS_INIT})

target_require_package(shuttlesock PUBLIC ev)
target_require_package(shuttlesock PRIVATE atomic_ops)

test_big_endian(SHUTTLESOCK_BIG_ENDIAN)

cmake_push_check_state(RESET)
check_symbol_exists(eventfd "sys/eventfd.h" SHUTTLESOCK_HAVE_EVENTFD)
cmake_reset_check_state()

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER  ${CCACHE_PROGRAM})
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

add_executable(shuso_test test/test.c)
target_link_libraries(shuso_test PRIVATE shuttlesock)
target_include_directories(shuso_test PRIVATE test)

configure_file(src/include/shuttlesock/configure.h.tmpl src/include/shuttlesock/configure.h)
