include(CheckCSourceRuns)
include(CMakePushCheckState)

function(test_accept4 result_var)
  message(STATUS "Check if system has accept4")
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_QUIET 1)
  #set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror -std=c11")
  check_c_source_compiles("
    #define _GNU_SOURCE
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <stddef.h>
    int main(void) {
      return accept4(0, NULL, NULL, SOCK_NONBLOCK);
    }
  " have_accept4)
  cmake_reset_check_state()
  
  if(have_accept4)
    message(STATUS "Check if system has accept4 - yes")
  else()
    message(STATUS "Check if system has accept4 - no")
  endif()
  set(${result_var} ${have_accept4} PARENT_SCOPE)
  unset(have_accept4 CACHE)
endfunction()
