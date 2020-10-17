

function(shuttlesock_link_picohttpparser)
  
  message(STATUS "Will build picohttpparser from source")
  
  #install header
  file(MAKE_DIRECTORY "${THIRDPARTY_PREFIX}/include")
  file(
    COPY "thirdparty/picohttpparser/picohttpparser.h" 
    DESTINATION "${THIRDPARTY_PREFIX}/include/"
  )
  
  add_library(picohttpparser STATIC 
    thirdparty/picohttpparser/picohttpparser.c
  )
  #if("${CMAKE_C_COMPILER_ID}" MATCHES "^(GNU)|((Apple)?Clang)$")
  #  target_compile_options(uring PRIVATE -Wno-pointer-arith)
  #endif()
  target_link_libraries(shuttlesock PRIVATE picohttpparser)
endfunction()
