include(CheckCSourceCompiles)
include(CMakePushCheckState)

function(test_SO_REUSEPORT reuseport_var)
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
  " have_so_reuseport)
  cmake_reset_check_state()
  
  if(have_so_reuseport)
    message(STATUS "Check if system supports SO_REUSEPORT - yes")
  else()
    message(STATUS "Check if system supports SO_REUSEPORT - no")
  endif()
  set(${reuseport_var} ${have_so_reuseport} PARENT_SCOPE)
  unset(have_so_reuseport CACHE)
endfunction()
