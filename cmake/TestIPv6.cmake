include(CheckCSourceRuns)
include(CMakePushCheckState)

function(test_ipv6 result_var)
  message(STATUS "Check if system supports IPv6")
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_QUIET 1)
  #set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror -std=c11")
  check_c_source_compiles("
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <stddef.h>
    int main(void) {
      int   inet6_code = AF_INET6;
      struct in6_addr *sa = NULL;
      return 0;
    }
  " have_ipv6)
  cmake_reset_check_state()
  
  if(have_ipv6)
    message(STATUS "Check if system supports IPv6 - yes")
  else()
    message(STATUS "Check if system supports IPv6 - no")
  endif()
  set(${result_var} ${have_ipv6} PARENT_SCOPE)
  unset(have_ipv6 CACHE)
endfunction()
