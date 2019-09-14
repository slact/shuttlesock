include(CheckCSourceRuns)
include(CMakePushCheckState)

function(test_type_generic_macros result_var)
  if(DEFINED ${result_var})
    #I'd rather use DEFINED CACHE{$var}, but that only got added in 3.14
    return()
  endif()
  message(STATUS "Check if compiler supports _Generic macros from C11")
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_QUIET 1)
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror -std=c11")
  check_c_source_compiles("
    #include <stddef.h>
    #define foo(x) _Generic((x),      int: foo_int, \
                                      char *: foo_charp, \
                                      char: foo_char \
                          )(x)
    int foo_int(int x) {
      return 1;
    }
    int foo_charp(char *x) {
      return 2;
    }
    int foo_char(char x) {
      return 3;
    }
    int main(void) {
      int   intx = 0;
      char *charpx = NULL;
      char  charx = '0';
      if(foo(intx) != 1) return 1;
      if(foo(charpx) != 2) return 1;
      if(foo(charx) != 3) return 1;
      return 0;
    }
  " "${result_var}")
  cmake_reset_check_state()
  
  if(${result_var})
    message(STATUS "Check if compiler supports _Generic macros from C11 - yes")
  else()
    message(STATUS "Check if compiler supports _Generic macros from C11 - no")
  endif()
endfunction()
