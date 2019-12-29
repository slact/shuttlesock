include(CheckCSourceRuns)
include(CMakePushCheckState)

function(test_machine_is_big_endian result_var)
  message(STATUS "Check if the system is big endian")
  
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_QUIET 1)
    check_c_source_runs("
    #include <inttypes.h>
    int main(int argc, char ** argv){
        volatile uint32_t i=0x01234567;
        // return 0 for big endian, 1 for little endian.
        return (*((uint8_t*)(&i))) == 0x67;
    }
  " is_big_endian)
  if(is_big_endian)
    message(STATUS "Check if the system is big endian - yes")
  else()
    message(STATUS "Check if the system is big endian - no, little endian")
  endif()
  set(${result_var} ${is_big_endian} CACHE INTERNAL "System is big endian")
  unset(is_big_endian CACHE)
endfunction()
