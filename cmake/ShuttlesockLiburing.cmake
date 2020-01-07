set(LIBURING_RELEASE_VERSION 0.3)
set(LIBURING_RELEASE_MD5 "")


function(shuttlesock_link_liburing STATIC_BUILD)
  
  if(NOT STATIC_BUILD)
    target_require_package(shuttlesock PUBLIC uring
      HEADER_NAME liburing.h
      OPTIONAL LIBURING_FOUND
      DRY_RUN
    )
    if(LIBURING_FOUND)
      target_require_package(shuttlesock PUBLIC uring HEADER_NAME liburing.h)
    else()
      message(STATUS "liburing not found. Will build from source.")
      set(STATIC_BUILD ON)
    endif()
  endif()
  
  if(STATIC_BUILD)
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
      CONFIGURE_COMMAND sh -c "CFLAGS=\"${SHUTTLESOCK_SHARED_CFLAGS} -O${OPTIMIZE_LEVEL} -w\" LDFLAGS=\"${SHUTTLESOCK_SHARED_LDFLAGS}\" CC=\"${SHUTTLESOCK_SHARED_CC}\" ./configure --prefix=${LIBURING_PREFIX_DIR}"
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
  endif()
endfunction()
