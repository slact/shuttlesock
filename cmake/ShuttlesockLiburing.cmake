set(LIBURING_RELEASE_VERSION 0.3)
set(LIBURING_RELEASE_MD5 "")


function(shuttlesock_link_liburing)
  
  include(ProcessorCount)
  ProcessorCount(processor_count)
  if(processor_count GREATER 1)
    set(LIBURING_MAKE_PARALLEL_FLAG -j${processor_count})
  endif()

  set(LIBURING_PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/liburing)
  
  include(ExternalProject)
  ExternalProject_Add(liburing
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

  add_dependencies(shuttlesock liburing)
  
  ExternalProject_Add_Step(liburing symlink_includes
      COMMAND ${CMAKE_COMMAND} -E create_symlink  "${LIBURING_PREFIX_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/src/include/shuttlesock/liburing"
    )
  target_include_directories(shuttlesock PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/src/include/shuttlesock/liburing")
  
  target_link_libraries(shuttlesock PRIVATE ${LIBURING_PREFIX_DIR}/lib/liburing.a)
  
endfunction()
