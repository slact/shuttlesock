set(LIBEV_RELEASE_VERSION "4.33")
set(LIBEV_RELEASE_MD5 a3433f23583167081bf4acdd5b01b34f)

set(LIBEV_MIN_VERSION "4.31")

include(TargetRequirePackage)
include(CheckCSourceRuns)

function(shuttlesock_link_libev STATIC_BUILD LIBEV_EXTRA_CFLAGS)

  if(NOT STATIC_BUILD)
    target_require_package(shuttlesock PUBLIC ev
      NAME "libev"
      OPTIONAL LIBEV_FOUND
      DRY_RUN
      INCLUDE_PATH_VAR libev_include_path
      LINK_LIB_VAR libev_lib_path
    )
    if(LIBEV_FOUND)
      if(NOT DEFINED LIBEV_MIN_VERSION_SATISFIED)
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
            printf(\"%d.%d\", (int)EV_VERSION_MAJOR, (int)EV_VERSION_MINOR);
            return (EV_VERSION_MAJOR >= ${ver_major} && EV_VERSION_MINOR >= ${ver_minor}) ? 0 : 1;
          }
        " LIBEV_MIN_VERSION_SATISFIED)
        set(LIBEV_MIN_VERSION_SATISFIED "${LIBEV_MIN_VERSION_SATISFIED}" CACHE INTERNAL "")
        set(LIBEV_FOUND_VERSION "${RUN_OUTPUT}" CACHE INTERNAL "")
        cmake_reset_check_state()
        if(LIBEV_MIN_VERSION_SATISFIED)
          message(STATUS "Check if libev version >= ${LIBEV_MIN_VERSION} - yes (${LIBEV_FOUND_VERSION})")
        else()
          message(STATUS "Check if libev version >= ${LIBEV_MIN_VERSION} - no (${LIBEV_FOUND_VERSION})")
        endif()        
      endif()
      
      if(LIBEV_MIN_VERSION_SATISFIED)
        target_require_package(shuttlesock PUBLIC ev QUIET)
      else()
        set(STATIC_BUILD "YES")
      endif()
    else()
      set(STATIC_BUILD "YES")
    endif()
  endif()
  
  if(STATIC_BUILD)
    message(STATUS "Will build libev ${LIBEV_RELEASE_VERSION} from source")
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
      CONFIGURE_COMMAND sh -c "CFLAGS=\"${SHUTTLESOCK_SHARED_CFLAGS} -O${OPTIMIZE_LEVEL} ${LIBEV_EXTRA_CFLAGS} -w\" LDFLAGS=\"${SHUTTLESOCK_SHARED_LDFLAGS}\" CC=\"${SHUTTLESOCK_SHARED_CC}\" ./configure --prefix=\"${THIRDPARTY_PREFIX}\" --enable-shared=no --with-pic=yes"
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
