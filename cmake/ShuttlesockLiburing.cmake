set(LIBURING_RELEASE_VERSION 0.3)
set(LIBURING_RELEASE_MD5 "")


function(shuttlesock_link_liburing)
  
  include(ProcessorCount)
  ProcessorCount(processor_count)
  if(NOT processor_count GREATER 1)
    set(LIBURING_MAKE_PARALLEL_FLAG -j${processor_count})
  endif()

  set(LIBURING_PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/liburing)
  
  include(ExternalProject)
  ExternalProject_Add(uring_static
    URL "https://git.kernel.dk/cgit/liburing/snapshot/liburing-${LIBURING_RELEASE_VERSION}.tar.gz"
    URL_MD5 ""
    DOWNLOAD_NO_PROGRESS 1
    PREFIX ${LIBURING_PREFIX_DIR}
    DOWNLOAD_DIR ${CMAKE_CURRENT_LIST_DIR}/.cmake_downloads
    CONFIGURE_COMMAND /bin/sh -c "CFLAGS=\"-O${OPTIMIZE_LEVEL} -w\" LDFLAGS=\"${SHUTTLESOCK_SHARED_LDFLAGS}\" CC=\"${SHUTTLESOCK_SHARED_CC}\" ./configure --prefix=${LIBURING_PREFIX_DIR}"
    BUILD_COMMAND make ${LIBEV_MAKE_PARALLEL_FLAG} -C src liburing.a
    INSTALL_COMMAND make install
    BUILD_BYPRODUCTS ${LIBURING_PREFIX_DIR}/lib/liburing.a
    BUILD_IN_SOURCE 1
  )

  add_dependencies(shuttlesock uring_static)
  target_include_directories(shuttlesock PRIVATE ${LIBURING_PREFIX_DIR}/include)
  target_link_libraries(shuttlesock PRIVATE ${LIBURING_PREFIX_DIR}/lib/liburing.a)
  
  #target_include_directories(shuttlesock PRIVATE lib/liburing/src)
  #liburing
  #add_library(uring STATIC 
  #  lib/liburing/src/queue.c
  #  lib/liburing/src/register.c
  #  lib/liburing/src/setup.c
  #  lib/liburing/src/syscall.c
  #)
  #if("${CMAKE_C_COMPILER_ID}" MATCHES "^(GNU)|((Apple)?Clang)$")
  #  target_compile_options(uring PRIVATE -Wno-pointer-arith)
  #endif()
  #target_link_libraries(shuttlesock PRIVATE uring)
endfunction()
