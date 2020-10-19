set(OPENSSL_RELEASE_VERSION "1.1.1d")
set(OPENSSL_RELEASE_SHA256 1e3a91bc1f9dfce01af26026f856e064eab4c8ee0a8f457b5ae30b40b8b711f2)

set(OPENSSL_MIN_VERSION "1.0.0")

function(shuttlesock_link_openssl STATIC_BUILD)  
  if(NOT STATIC_BUILD)
    if(NOT DEFINED OPENSSL_ROOT_DIR)
      if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        #add some homebrew path hints
        #find_program(BREW_PROGRAM NAMES brew HINTS /usr/local/bin/)
        #if(BREW_PROGRAM)
        #  execute_process(
        #    COMMAND ${BREW_PROGRAM} --prefix openssl
        #    OUTPUT_VARIABLE brew_output
        #    RESULT_VARIABLE brew_result
        #  )
        #  if(RESULT_VARIABLE EQUAL 0)
        #    set(OPENSSL_ROOT_DIR ${brew_output} CACHE INTERNAL "OpenSSL root dir")
        #  endif()
        #endif()
        set(OPENSSL_ROOT_DIR /usr/local/opt/openssl)
      endif()
    endif()
    set(OpenSSL_FIND_QUIETLY YES)
    message(STATUS "Check if OpenSSL is installed")
    include(FindOpenSSL)
    if(OPENSSL_FOUND)
      message(STATUS "Check if OpenSSL is installed - yes")
      message(STATUS "Check if OpenSSL version >= ${OPENSSL_MIN_VERSION}")
      if("${OPENSSL_VERSION}" VERSION_GREATER "${OPENSSL_MIN_VERSION}" OR "${OPENSSL_VERSION}" VERSION_EQUAL "${OPENSSL_MIN_VERSION}")
        message(STATUS "Check if OpenSSL version >= ${OPENSSL_MIN_VERSION} - yes")
        target_include_directories(shuttlesock PUBLIC ${OPENSSL_INCLUDE_DIR})
        target_link_libraries(shuttlesock PUBLIC ${OPENSSL_LIBRARIES})
      else()
        message(STATUS "Check if OpenSSL version >= ${OPENSSL_MIN_VERSION} - no")
        set(STATIC_BUILD ON)
      endif()
    else()
      message(STATUS "Check if OpenSSL is installed - no")
      set(STATIC_BUILD ON)
    endif()
  endif()
  if(STATIC_BUILD)
    message(STATUS "Will build OpenSSL ${OPENSSL_RELEASE_VERSION} from source")
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
