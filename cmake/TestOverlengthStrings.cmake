include(CheckCSourceCompiles)
include(CMakePushCheckState)

function(test_overlength_strings overlength_strings_var)
  message(STATUS "Check if compiler supports -Wno-overlength-strings")
  cmake_push_check_state(RESET)
  
  set(C_SOURCE "#include <stdio.h>
  const char foo[]=\"")
  foreach(v RANGE 20000)
    string(APPEND C_SOURCE "q")
  endforeach(v)
  string(APPEND C_SOURCE "\";")
  string(APPEND C_SOURCE "
    int main(void) {
      printf(\"%s\", foo);
      return 0;
    }
  ")
  
  set(CMAKE_REQUIRED_QUIET 1)
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror -Wall -Wextra -pedantic -std=c11")
  check_c_source_compiles("${C_SOURCE}" COMPILES_WITHOUT_NO_OVERLENGTH_STRINGS)
  cmake_reset_check_state()
  
  set(CMAKE_REQUIRED_QUIET 1)
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror -Wall -Wextra -pedantic -std=c11 -Wno-overlength-strings")
  check_c_source_compiles("${C_SOURCE}" COMPILES_WITH_NO_OVERLENGTH_STRINGS)
  cmake_reset_check_state()
  
  if((NOT COMPILES_WITHOUT_NO_OVERLENGTH_STRINGS) AND COMPILES_WITH_NO_OVERLENGTH_STRINGS)
    set(${overlength_strings_var} "yes" PARENT_SCOPE)
    message(STATUS "Check if compiler supports -Wno-overlength-strings - yes")
  else()
    set(${overlength_strings_var} "" PARENT_SCOPE)
    message(STATUS "Check if compiler supports -Wno-overlength-strings - no")
  endif()
  unset(COMPILES_WITH_NO_OVERLENGTH_STRINGS CACHE)
endfunction()
