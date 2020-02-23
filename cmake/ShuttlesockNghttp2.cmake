set(NGHTTP2_RELEASE_VERSION 1.40.0)
set(NGHTTP2_RELEASE_MD5 "5df375bbd532fcaa7cd4044b54b1188d")

include(CheckCSourceRuns)
include(CMakePushCheckState)

function(shuttlesock_link_nghttp2 STATIC_BUILD)
  if(NOT STATIC_BUILD)
    target_require_package(shuttlesock PUBLIC nghttp2
      HEADER_NAME nghttp2/nghttp2.h
      DISPLAY_NAME "nghttp2"
      DRY_RUN
      OPTIONAL NGHTTP2_FOUND
    )
  endif()
  if(NGHTTP2_FOUND)
    target_require_package(shuttlesock PUBLIC nghttp2 HEADER_NAME nghttp2/nghttp2.h QUIET)
  else()
    include(ExternalProject)
    
    set(NGHTTP2_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/nghttp2)
    ExternalProject_Add(nghttp2
      URL "https://github.com/nghttp2/nghttp2/releases/download/v${NGHTTP2_RELEASE_VERSION}/nghttp2-${NGHTTP2_RELEASE_VERSION}.tar.gz"
      URL_MD5 "${NGHTTP2_RELEASE_MD5}"
      DOWNLOAD_NO_PROGRESS 1
      PREFIX "${NGHTTP2_PREFIX}"
      DOWNLOAD_DIR "${THIRDPARTY_DOWNLOAD}"
      CMAKE_ARGS
        "-DCMAKE_C_FLAGS=${SHUTTLESOCK_SHARED_CFLAGS}"
        -DENABLE_STATIC_LIB=ON
        -DENABLE_SHARED_LIB=OFF
        -DENABLE_APP=OFF
        -DENABLE_EXAMPLES=OFF
        -DENABLE_ASIO_LIB=OFF
        -DENABLE_HPACK_TOOLS=OFF
        -DENABLE_PYTHON_BINDINGS=OFF
        -DWITH_LIBXML2=OFF
        -DWITH_JEMALLOC=OFF
        -DWITH_MRUBY=OFF
        "-DCMAKE_INSTALL_PREFIX=${THIRDPARTY_PREFIX}"
      BUILD_BYPRODUCTS ${THIRDPARTY_PREFIX}/lib/libnghttp2_static.a
    )
    
    add_dependencies(shuttlesock nghttp2)
    
    target_link_libraries(shuttlesock PUBLIC ${THIRDPARTY_PREFIX}/lib/libnghttp2_static.a)
  endif()
endfunction()
