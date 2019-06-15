include(CheckCSourceCompiles)
include(CMakePushCheckState)

function(test_typeof_keyword typeof_keyword_var)
  if(DEFINED ${typeof_keyword_var})
    #I'd rather use DEFINED CACHE{$var}, but that only got added in 3.14
    return()
  endif()
  message(STATUS "Check if __typeof__() exist")
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_QUIET 1)
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror -std=c11")
  check_c_source_compiles("
    int main(void) {
      int *foo;
      __typeof__(*foo) bar;
      return 0;
    }
  " "${typeof_keyword_var}")
  cmake_reset_check_state()
  
  if(${typeof_keyword_var})
    message(STATUS "Check if __typeof__() exist - yes")
  else()
    message(STATUS "Check if __typeof__() exist - no")
  endif()
endfunction()
