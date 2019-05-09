cmake_minimum_required(VERSION 3.4)
set(CMAKE_MODULE_PATH "./cmake")
project(shuttlesock LANGUAGES "C" VERSION 0.0.1)
set(DESCRIPTION "speedy server library with a focus on moving sockets between processes")

target_compile_features(shuttlesock c_std_99 c_static_assert c_variadic_macros)

#set(CMAKE_VERBOSE_MAKEFILE ON)
set(libsrc 
  src/shuttlesock.c
)

add_library(shuttlesock ${libsrc})

target_include_directories(shuttlesock PUBLIC src/include)
target_include_directories(shuttlesock PRIVATE src)


include(CheckSymbolExists)
include(TestBigEndian)
include(GNUInstallDirs)
include(TargetRequirePackage)

target_require_package(shuttlesock ev)
target_require_package(shuttlesock atomic_ops)

test_big_endian(SHUSO_BIG_ENDIAN)

configure_file(src/configure.h.tmpl src/configure.h)
