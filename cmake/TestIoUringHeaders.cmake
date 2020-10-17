include(CheckCSourceRuns)
include(CMakePushCheckState)

function(test_io_uring_buildable result_var)
  message(STATUS "Check if liburing may be used")
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_QUIET 1)
  set(CMAKE_REQUIRED_INCLUDES lib/liburing/src)
  check_c_source_runs("
    #include <linux/fs.h>
    #include <linux/types.h>
    int main(void) {
      return 0;
    }
  " io_uring_headers_present)
  cmake_reset_check_state()
  if(io_uring_headers_present)
    message(STATUS "Check if liburing may be used - yes")
    set(${result_var} YES PARENT_SCOPE)
  else()
    message(STATUS "Check if liburing may be used - no")
    set(${result_var} NO PARENT_SCOPE)
  endif()
  unset(io_uring_headers_present CACHE)
endfunction()
