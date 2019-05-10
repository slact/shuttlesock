cmake_minimum_required(VERSION 3.4)
set(CMAKE_MODULE_PATH "./cmake")
project(shuttlesock LANGUAGES "C" VERSION 0.0.1)
set(DESCRIPTION "speedy server library with a focus on moving sockets between processes")

#set(CMAKE_VERBOSE_MAKEFILE ON)
set(libsrc 
  src/shuttlesock.c
)

add_library(shuttlesock ${libsrc})
target_compile_features(shuttlesock PRIVATE c_std_99 c_static_assert c_variadic_macros)

target_include_directories(shuttlesock PUBLIC src/include)
target_include_directories(shuttlesock PRIVATE src)


include(CheckSymbolExists)
include(TestBigEndian)
include(GNUInstallDirs)
include(TargetRequirePackage)

target_require_package(shuttlesock PUBLIC ev)
target_require_package(shuttlesock PRIVATE atomic_ops)

test_big_endian(SHUSO_BIG_ENDIAN)

configure_file(src/configure.h.tmpl src/configure.h)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(C_LAUNCHER   "${CCACHE_PROGRAM}")
  set(CXX_LAUNCHER "${CCACHE_PROGRAM}")
endif()


add_executable(shuso_test test/test.c)
target_link_libraries(shuso_test PRIVATE shuttlesock)
target_include_directories(shuso_test PRIVATE test)
