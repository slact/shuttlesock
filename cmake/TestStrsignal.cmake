include(CheckCSourceRuns)
include(CMakePushCheckState)

function(test_strsignal result_var)
  message(STATUS "Check if system has strsignal()")
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_QUIET 1)
  #set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror -std=c11")
  check_c_source_compiles("
    #ifndef _GNU_SOURCE
    #define _GNU_SOURCE
    #endif
    #ifndef _POSIX_C_SOURCE
    #define _POSIX_C_SOURCE 200809L
    #endif
    #include <string.h>
    #include <stddef.h>
    int main(void) {
      const char *sigstr = strsignal(9);
      return sigstr != NULL;
    }
  " have_strsignal)
  cmake_reset_check_state()
  
  if(have_strsignal)
    message(STATUS "Check if system has strsignal() - yes")
  else()
    message(STATUS "Check if system has strsignal() - no")
  endif()
  set(${result_var} ${have_strsignal} PARENT_SCOPE)
  unset(have_strsignal CACHE)
endfunction()
