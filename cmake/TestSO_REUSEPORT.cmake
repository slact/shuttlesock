include(CheckCSourceCompiles)
include(CMakePushCheckState)

function(test_SO_REUSEPORT reuseport_var)
  if(DEFINED ${reuseport_var})
    #I'd rather use DEFINED CACHE{$var}, but that only got added in 3.14
    return()
  endif()
  message(STATUS "Check if system supports SO_REUSEPORT")
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_QUIET 1)
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror -std=gnu11")
  check_c_source_compiles("
    #include <sys/socket.h>
    #ifndef SO_REUSEPORT
    #error SO_REUSEPORT missing
    #endif
    int main(void) {
      return 0;
    }
  " "${reuseport_var}")
  cmake_reset_check_state()
  
  if(${reuseport_var})
    message(STATUS "Check if system supports SO_REUSEPORT - yes")
  else()
    message(STATUS "Check if system supports SO_REUSEPORT - no")
  endif()
endfunction()
