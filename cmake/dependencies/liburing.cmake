set(LIBURING_RELEASE_VERSION 0.7)

function(shuttlesock_link_liburing STATIC_BUILD)
  
  if(NOT STATIC_BUILD)
    target_require_package(shuttlesock PUBLIC uring
      HEADER_NAME liburing.h
      OPTIONAL LIBURING_FOUND
      DRY_RUN
      INCLUDE_PATH_VAR liburing_include_path
      LINK_LIB_VAR liburing_lib_path
    )
    
    if(LIBURING_FOUND)
      if(NOT DEFINED LIBURING_HAS_OPCODE_SUPPORTED)
        message(STATUS "Check if liburing has io_uring_opcode_supported()")
        cmake_push_check_state(RESET)
        set(CMAKE_REQUIRED_QUIET 1)
        set(CMAKE_REQUIRED_INCLUDES ${liburing_include_path})
        set(CMAKE_REQUIRED_LIBRARIES ${liburing_lib_path})
        check_c_source_runs("
          #include <stdlib.h>
          #include <liburing.h>
          int main(void) {
            struct io_uring_probe *probe = io_uring_get_probe();
            if(probe) {
              io_uring_opcode_supported(probe, IORING_OP_NOP);
              free(probe);
            }
            return 0;
          }
        " LIBURING_HAS_OPCODE_SUPPORTED)
        set(LIBURING_HAS_OPCODE_SUPPORTED "${LIBURING_HAS_OPCODE_SUPPORTED}" CACHE INTERNAL "")
        cmake_reset_check_state()
        if(LIBURING_HAS_OPCODE_SUPPORTED)
          message(STATUS "Check if liburing has io_uring_opcode_supported() - yes")
          set(${result_var} YES PARENT_SCOPE)
        else()
          message(STATUS "Check if liburing has io_uring_opcode_supported() - no")
          set(${result_var} NO PARENT_SCOPE)
        endif()
      endif()
      
      if(LIBURING_HAS_OPCODE_SUPPORTED)
        target_require_package(shuttlesock PUBLIC uring HEADER_NAME liburing.h QUIET)
      else()
        set(STATIC_BUILD "YES")
      endif()
    else()
      set(STATIC_BUILD ON)
    endif()
  endif()
  
  if(STATIC_BUILD)
    message(STATUS "Will build liburing ${LIBURING_RELEASE_VERSION} from source")
    include(ProcessorCount)
    ProcessorCount(processor_count)
    if(processor_count GREATER 1)
      set(LIBURING_MAKE_PARALLEL_FLAG -j${processor_count})
    endif()

    set(LIBURING_PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/liburing)
    
    include(ExternalProject)
    ExternalProject_Add(liburing
      GIT_REPOSITORY "https://git.kernel.dk/liburing"
      GIT_TAG "liburing-${LIBURING_RELEASE_VERSION}"
      DOWNLOAD_NO_PROGRESS 1
      PREFIX ${LIBURING_PREFIX_DIR}
      DOWNLOAD_DIR "${THIRDPARTY_DOWNLOAD}"
      CONFIGURE_COMMAND sh -c "CFLAGS=\"${SHUTTLESOCK_SHARED_CFLAGS} -O${OPTIMIZE_LEVEL} -w\" LDFLAGS=\"${SHUTTLESOCK_SHARED_LDFLAGS}\" CC=\"${SHUTTLESOCK_SHARED_CC}\" ./configure --prefix=${THIRDPARTY_PREFIX}"
      BUILD_COMMAND make ${LIBEV_MAKE_PARALLEL_FLAG} -C src liburing.a
      INSTALL_COMMAND make install
      BUILD_BYPRODUCTS ${THIRDPARTY_PREFIX}/lib/liburing.a
      BUILD_IN_SOURCE 1
    )
    add_dependencies(shuttlesock liburing)
    
    target_link_libraries(shuttlesock PRIVATE ${THIRDPARTY_PREFIX}/lib/liburing.a)
  endif()
endfunction()
