set(HYPERSCAN_RELEASE_VERSION 5.2.1)
set(HYPERSCAN_RELEASE_MD5 "e722ec217282d38b1457cc751f0a4bb6")

include(CheckCSourceRuns)
include(CMakePushCheckState)

function(shuttlesock_link_hyperscan STATIC_BUILD)
  if(NOT STATIC_BUILD)
    target_require_package(shuttlesock PUBLIC hyperscan
      LIB_NAME
        hs hs_runtime
      HEADER_NAME
        hs.h hs_compile.h hs_runtime.h
      DRY_RUN
      OPTIONAL HYPERSCAN_FOUND
    )
  endif()
  if(HYPERSCAN_FOUND)
    target_require_package(shuttlesock PUBLIC hyperscan
      LIB_NAME
        hs hs_runtime
      HEADER_NAME
        hs.h hs_compile.h hs_runtime.h
    )
    target_link_libraries(shuttlesock PUBLIC hyperscan)
  else()
    
    find_program(RAGEL_BINARY NAMES ragel)
    if(NOT RAGEL_BINARY)
      message(FATAL_ERROR "Ragel is not installed, but is required to build Hyperscan")
    endif()
    
    include(ExternalProject)
    
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
      set(fat_runtime -DFAT_RUNTIME=ON)
    else()
      set(fat_runtime -DFAT_RUNTIME=OFF)
    endif()
    
    
    if(CMAKE_C_COMPILER_LAUNCHER)
      set(maybe_ccache "-DCMAKE_C_COMPILER_LAUNCHER=${CMAKE_C_COMPILER_LAUNCHER}" "-DCMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}")
    endif()
    
    set(HYPERSCAN_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/hyperscan)
    ExternalProject_Add(hyperscan
      URL "https://github.com/intel/hyperscan/archive/v${HYPERSCAN_RELEASE_VERSION}.tar.gz"
      URL_MD5 "${HYPERSCAN_RELEASE_MD5}"
      DOWNLOAD_NO_PROGRESS 1
      PREFIX "${HYPERSCAN_PREFIX}"
      DOWNLOAD_DIR "${THIRDPARTY_DOWNLOAD}"
      CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF
        "-DCMAKE_INSTALL_PREFIX=${THIRDPARTY_PREFIX}"
        ${maybe_ccache}
        ${fat_runtime}
      BUILD_BYPRODUCTS
        ${THIRDPARTY_PREFIX}/lib/libhs.a
        ${THIRDPARTY_PREFIX}/lib/libhs_runtime.a
    )
    
    add_dependencies(shuttlesock hyperscan)
    
    target_link_libraries(shuttlesock PUBLIC ${THIRDPARTY_PREFIX}/lib/libhs.a)
    target_link_libraries(shuttlesock PUBLIC ${THIRDPARTY_PREFIX}/lib/libhs_runtime.a)
  endif()
endfunction()
