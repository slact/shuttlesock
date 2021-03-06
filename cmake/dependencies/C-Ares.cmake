set(C_ARES_RELEASE_VERSION 1.16.1)
set(C_ARES_RELEASE_MD5 "62dece8675590445d739b23111d93692")

set(C_ARES_MIN_VERSION 1.13.0)

include(CheckCSourceRuns)
include(CMakePushCheckState)

function(test_c_ares_version_min c_ares_include_path c_ares_lib_path c_ares_version_check_result_var)

  string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$" c_ares_version_match "${C_ARES_MIN_VERSION}")
  set(ver_major ${CMAKE_MATCH_1})
  set(ver_minor ${CMAKE_MATCH_2})
  set(ver_patch ${CMAKE_MATCH_3})
  
  set(c_ares_var "C_ARES_VERSION_MIN_${ver_major}_${ver_minor}_${ver_patch}")
  if(DEFINED ${c_ares_var})
    #I'd rather use DEFINED CACHE{$var}, but that only got added in 3.14
    set(${c_ares_version_check_result_var} ${${c_ares_var}} PARENT_SCOPE)
    return()
  endif()
  message(STATUS "Check if c-ares version >= ${ver_major}.${ver_minor}.${ver_patch}")
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_QUIET 1)
  set(CMAKE_REQUIRED_INCLUDES "${c_ares_include_path}")
  set(CMAKE_REQUIRED_LIBRARIES "${c_ares_lib_path}")
  check_c_source_runs("
    #include <ares_version.h>
    #include <stdio.h>
    int main(void) {
      printf(\"%d.%d.%d\", ARES_VERSION_MAJOR, ARES_VERSION_MINOR, ARES_VERSION_PATCH);
      return !(ARES_VERSION_MAJOR >= ${ver_major} && ARES_VERSION_MINOR >= ${ver_minor} && ARES_VERSION_PATCH >= ${ver_patch});
    }
  " "${c_ares_var}")
  set(c_ares_version_found "${RUN_OUTPUT}")
  cmake_reset_check_state()
  if("${${c_ares_var}}")
    message(STATUS "Check if c-ares version >= ${ver_major}.${ver_minor}.${ver_patch} - yes (${c_ares_version_found})")
    set(${c_ares_version_check_result_var} "YES" PARENT_SCOPE)
  else()
    message(STATUS "Check if c-ares version >= ${ver_major}.${ver_minor}.${ver_patch} - no (${c_ares_version_found})")
    set(${c_ares_version_check_result_var} "NO" PARENT_SCOPE)
  endif()
endfunction()

function(shuttlesock_link_c_ares STATIC_BUILD)
  #version 1.13.0 is where ares_set_socket_functions got added, which we need
  if(NOT STATIC_BUILD)
    target_require_package(shuttlesock PUBLIC cares HEADER_NAME ares.h
      DISPLAY_NAME "c-ares"
      DRY_RUN
      OPTIONAL C_ARES_FOUND
      INCLUDE_PATH_VAR cares_include_path
      LINK_LIB_VAR cares_lib_path
    )
    if(C_ARES_FOUND)
      test_c_ares_version_min("${cares_include_path}" "${cares_lib_path}" C_ARES_VERSION_OK)
    endif()
  endif()
  if(C_ARES_FOUND AND C_ARES_VERSION_OK)
    target_require_package(shuttlesock PUBLIC cares HEADER_NAME ares.h QUIET)
  else()
    message(STATUS "Will build c-ares ${C_ARES_RELEASE_VERSION} from source")
    include(ExternalProject)
    
    if(CMAKE_C_COMPILER_LAUNCHER)
      set(maybe_ccache "-DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}" "-DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}")
    endif()
    
    #message(FATAL_ERROR "SHUTTLESOCK_SHARED_LDFLAGS: ${SHUTTLESOCK_SHARED_LDFLAGS}")
    
    set(C_ARES_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/c_ares)
    ExternalProject_Add(c_ares
      URL "https://c-ares.haxx.se/download/c-ares-${C_ARES_RELEASE_VERSION}.tar.gz"
      URL_MD5 "${C_ARES_RELEASE_MD5}"
      PREFIX "${C_ARES_PREFIX}"
      DOWNLOAD_DIR "${THIRDPARTY_DOWNLOAD}"
      CMAKE_ARGS
        "-DCMAKE_C_FLAGS=${SHUTTLESOCK_SHARED_CFLAGS}"
        -DCARES_STATIC=ON
        -DCARES_SHARED=OFF
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCARES_BUILD_TOOLS=OFF
        "-DCMAKE_INSTALL_PREFIX=${THIRDPARTY_PREFIX}"
        ${maybe_ccache}
      BUILD_BYPRODUCTS ${THIRDPARTY_PREFIX}/lib/libcares.a
    )
    
    add_dependencies(shuttlesock c_ares)
    
    target_link_libraries(shuttlesock PUBLIC ${THIRDPARTY_PREFIX}/lib/libcares.a)
  endif()
endfunction()
