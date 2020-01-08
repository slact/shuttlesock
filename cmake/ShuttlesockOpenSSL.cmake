set(OPENSSL_RELEASE_VERSION "1.1.1d")
set(OPENSSL_RELEASE_SHA256 1e3a91bc1f9dfce01af26026f856e064eab4c8ee0a8f457b5ae30b40b8b711f2)

set(OPENSSL_MIN_VERSION "1.0.0")

function(shuttlesock_link_openssl STATIC_BUILD)  
  if(NOT STATIC_BUILD)
    include(FindOpenSSL)
    if(OPENSSL_FOUND)
      target_include_directories(shuttlesock PUBLIC ${OPENSSL_INCLUDE_DIR})
      target_link_libraries(shuttlesock PUBLIC ${OPENSSL_LIBRARIES})
    else()
      message(STATUS "Failed to find OpenSSL. Will build from source.")
      set(STATIC_BUILD ON)
    endif()
  endif()
  if(STATIC_BUILD)
    include(ProcessorCount)
    ProcessorCount(processor_count)
    if(processor_count GREATER 1)
      set(OPENSSL_MAKE_PARALLEL_FLAG -j${processor_count})
    endif()
    
    set(OPENSSL_PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/openssl)
    
    if(SHUTTLESOCK_SHARED_CFLAGS MATCHES "-fsanitize=undefined")
      set(OPENSSL_PEDANTIC_FLAG "-DPEDANTIC")
    endif()
    
    include(ExternalProject)
    ExternalProject_Add(openssl
      URL "https://www.openssl.org/source/openssl-${OPENSSL_RELEASE_VERSION}.tar.gz"
      URL_HASH "SHA256=${OPENSSL_RELEASE_SHA256}"
      DOWNLOAD_NO_PROGRESS 1
      DOWNLOAD_DIR "${THIRDPARTY_DOWNLOAD}"
      PREFIX ${OPENSSL_PREFIX_DIR}
      CONFIGURE_COMMAND
        ./config no-shared no-ssl2 "--prefix=${THIRDPARTY_PREFIX}" ${SHUTTLESOCK_SHARED_CFLAGS} ${SHUTTLESOCK_SHARED_LDFLAGS} ${OPENSSL_PEDANTIC_FLAG}
      BUILD_COMMAND make 
        "CC=${SHUTTLESOCK_SHARED_CC}"
        ${OPENSSL_MAKE_PARALLEL_FLAG}
        build_libs
      INSTALL_COMMAND make install_dev
      BUILD_BYPRODUCTS
        ${THIRDPARTY_PREFIX}/lib/libssl.a
        ${THIRDPARTY_PREFIX}/lib/libcrypto.a
      BUILD_IN_SOURCE 1
    )
    
    target_link_libraries(shuttlesock PUBLIC ${THIRDPARTY_PREFIX}/lib/libssl.a)
    target_link_libraries(shuttlesock PUBLIC ${THIRDPARTY_PREFIX}/lib/libcrypto.a)
    
    add_dependencies(shuttlesock openssl)
  endif()
endfunction()
