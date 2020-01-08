set(LIBEV_RELEASE_VERSION "4.31")
set(LIBEV_RELEASE_MD5 20111fda0df0a289c152faa2aac91b08)

set(LIBEV_MIN_VERSION "4.31")

include(TargetRequirePackage)
include(CheckCSourceRuns)

function(shuttlesock_link_libev STATIC_BUILD)

  if(NOT STATIC_BUILD)
    target_require_package(shuttlesock PUBLIC ev
      OPTIONAL LIBEV_FOUND
      DRY_RUN
      INCLUDE_PATH_VAR libev_include_path
      LINK_LIB_VAR libev_lib_path
    )
    if(LIBEV_FOUND)
      message(STATUS "Check if libev version >= ${LIBEV_MIN_VERSION}")
      cmake_push_check_state(RESET)
      set(CMAKE_REQUIRED_QUIET 1)
      set(CMAKE_REQUIRED_INCLUDES "${libev_include_path}")
      set(CMAKE_REQUIRED_LIBRARIES "${libev_lib_path}")
      string(REGEX MATCH "^([0-9]+)\\.([0-9]+)$" libev_version_match "${LIBEV_MIN_VERSION}")
      set(ver_major ${CMAKE_MATCH_1})
      set(ver_minor ${CMAKE_MATCH_2})
      check_c_source_runs("
        #include <ev.h>
        #include <stdio.h>
        int main(void) {
          printf(\"%d.%d\", EV_VERSION_MAJOR, EV_VERSION_MINOR);
          return !(EV_VERSION_MAJOR >= ${ver_major} && EV_VERSION_MINOR >= ${ver_minor});
        }
      " libev_version_ok_result)
      set(libev_version_found "${RUN_OUTPUT}")
      cmake_reset_check_state()
      
      if(libev_version_ok_result)
        message(STATUS "Check if libev version >= ${LIBEV_MIN_VERSION} - yes (${libev_version_found})")
        target_require_package(shuttlesock PUBLIC ev)
      else()
        message(STATUS "Check if libev version >= ${LIBEV_MIN_VERSION} - no (${libev_version_found}). Will build it from source.")
        set(STATIC_BUILD "YES")
      endif()
    else()
      message(STATUS "Did not find libev installed, will build it from source.")
      set(STATIC_BUILD "YES")
    endif()
  endif()
  
  if(STATIC_BUILD)
    include(ProcessorCount)
    ProcessorCount(processor_count)
    if(NOT processor_count GREATER 1)
      set(LIBEV_MAKE_PARALLEL_FLAG -j${processor_count})
    endif()
    
    set(LIBEV_PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/libev)
    include(ExternalProject)
    ExternalProject_Add(libev
      URL "http://dist.schmorp.de/libev/Attic/libev-${LIBEV_RELEASE_VERSION}.tar.gz"
      URL_MD5 ${LIBEV_RELEASE_MD5}
      DOWNLOAD_NO_PROGRESS 1
      DOWNLOAD_DIR "${THIRDPARTY_DOWNLOAD}"
    #  SOURCE_DIR ${LIBEV_DIR}
      CONFIGURE_COMMAND sh -c "CFLAGS=\"${SHUTTLESOCK_SHARED_CFLAGS} -O${OPTIMIZE_LEVEL} -w\" LDFLAGS=\"${SHUTTLESOCK_SHARED_LDFLAGS}\" CC=\"${SHUTTLESOCK_SHARED_CC}\" ./configure --prefix=\"${THIRDPARTY_PREFIX}\" --enable-shared=no --with-pic=yes"
      PREFIX ${LIBEV_PREFIX_DIR}
      BUILD_COMMAND make ${LIBEV_MAKE_PARALLEL_FLAG}
      INSTALL_COMMAND make install
      BUILD_BYPRODUCTS ${THIRDPARTY_PREFIX}/lib/libev.a
      BUILD_IN_SOURCE 1
    )
    
    target_link_libraries(shuttlesock PUBLIC ${THIRDPARTY_PREFIX}/lib/libev.a)
    
    add_dependencies(shuttlesock libev)
  endif()
endfunction()
