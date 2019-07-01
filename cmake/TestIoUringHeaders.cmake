include(CheckCSourceRuns)
include(CMakePushCheckState)

function(test_io_uring_buildable result_var)
  if(DEFINED ${result_var})
    #I'd rather use DEFINED CACHE{$var}, but that only got added in 3.14
    return()
  endif()
  message(STATUS "Check if liburing can be built")
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_QUIET 1)
  check_c_source_runs("
    #include <linux/fs.h>
    #include <linux/types.h>
    int main(void) {
      return 0;
    }
  " "${result_var}")
  cmake_reset_check_state()
  if(${result_var})
    message(STATUS "Check if liburing can be built - yes")
    set(${result_var} ON CACHE INTERNAL "Can build io_uring")
  else()
    message(STATUS "Check if liburing can be built - no")
    set(${result_var} OFF CACHE INTERNAL "Can build io_uring")
  endif()
endfunction()
