function(shuttlesock_link_liburing)
  target_include_directories(shuttlesock PRIVATE lib/liburing/src)
  #liburing
  add_library(uring STATIC 
    lib/liburing/src/queue.c
    lib/liburing/src/register.c
    lib/liburing/src/setup.c
    lib/liburing/src/syscall.c
  )
  if("${CMAKE_C_COMPILER_ID}" MATCHES "^(GNU)|((Apple)?Clang)$")
    target_compile_options(uring PRIVATE -Wno-pointer-arith)
  endif()
  target_link_libraries(shuttlesock PRIVATE uring)
endfunction()
