set(PCRE2_RELEASE_VERSION 10.34)
set(PCRE2_RELEASE_MD5 "fdb10dba7f3be43730966bebdd3755ef")

include(CheckCSourceRuns)
include(CMakePushCheckState)

function(shuttlesock_link_pcre STATIC_BUILD)
  if(NOT STATIC_BUILD)
    target_require_package(shuttlesock PUBLIC pcre2-posix HEADER_NAME pcre2posix.h
      DISPLAY_NAME "pcre2 (posix)"
      DRY_RUN
      OPTIONAL PCRE2_FOUND
    )
  endif()
  if(PCRE2_FOUND)
    target_require_package(shuttlesock PUBLIC pcre2-posix HEADER_NAME pcre2posix.h QUIET)
    target_link_libraries(shuttlesock PUBLIC pcre2-posix)
  else()
    include(ExternalProject)

    set(PCRE2_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/pcre2)
    ExternalProject_Add(pcre2
      URL "ftp://ftp.pcre.org/pub/pcre/pcre2-10.34.zip"
      URL_MD5 "${PCRE2_RELEASE_MD5}"
      DOWNLOAD_NO_PROGRESS 1
      PREFIX "${PCRE2_PREFIX}"
      DOWNLOAD_DIR "${THIRDPARTY_DOWNLOAD}"
      CMAKE_ARGS
        -DPCRE2_SUPPORT_JIT=ON
        -DPCRE2_BUILD_PCRE2GREP=OFF
        -DPCRE2_BUILD_TESTS=OFF
        "-DPCRE2_SUPPORT_VALGRIND=${SHUTTLESOCK_DEBUG_VALGRIND}"
        "-DCMAKE_INSTALL_PREFIX=${THIRDPARTY_PREFIX}"
      BUILD_BYPRODUCTS ${THIRDPARTY_PREFIX}/lib/libpcre2-posix.a
    )
    
    add_dependencies(shuttlesock pcre2)
    
    target_link_libraries(shuttlesock PUBLIC ${THIRDPARTY_PREFIX}/lib/libpcre2-posix.a)
  endif()
endfunction()
