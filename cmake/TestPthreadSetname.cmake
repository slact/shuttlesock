include(CheckCSourceCompiles)
include(CMakePushCheckState)

function(test_pthread_setname thread_libs style_var pthread_np_var)
  message(STATUS "Finding pthread setname style")
  cmake_push_check_state(RESET)
  #set(CMAKE_REQUIRED_QUIET 1)
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror -std=c11")
  set(CMAKE_REQUIRED_LIBRARIES "${CMAKE_THREAD_LIBS_INIT}")
  check_c_source_compiles("
    #define _GNU_SOURCE
    #include <pthread.h>
    int main(void) {
      pthread_t th;
      pthread_setname_np(pthread_self(), \"foobar\");
      return 0;
    }
  " PTHREAD_SETNAME_LINUX)
  
  check_c_source_compiles("
    #include <pthread.h>
    int main(void) {
      pthread_t th;
      pthread_setname_np(\"foobar\");
      return 0;
    }
  " PTHREAD_SETNAME_OSX)

  check_c_source_compiles("
    #include <pthread.h>
    #include <pthread_np.h>
    int main(void) {
      pthread_t th;
      pthread_set_name_np(pthread_self(), \"foobar\");
      return 0;
    }
  " PTHREAD_SETNAME_FREEBSD)

  check_c_source_compiles("
    #include <pthread.h>
    #include <pthread_np.h>
    int main(void) {
      pthread_t th;
      pthread_setname_np(pthread_self(), \"%s\", \"foobar\");
      return 0;
    }
  " PTHREAD_SETNAME_NETBSD)
  cmake_reset_check_state()
  
  if(PTHREAD_SETNAME_LINUX)
    set(${style_var} LINUX PARENT_SCOPE)
    set(${pthread_np_var} "" PARENT_SCOPE)
    message(STATUS "Finding pthread setname style - Linux")
  elseif(PTHREAD_SETNAME_OSX)
    set(${style_var} FREEBSD PARENT_SCOPE)
    set(${pthread_np_var} "TRUE" PARENT_SCOPE)
    message(STATUS "Finding pthread setname style - FreeBSD")
  elseif(PTRHEAD_SETNAME_FREEBSD)
    set(${style_var} OSX PARENT_SCOPE)
    set(${pthread_np_var} "" PARENT_SCOPE)
    message(STATUS "Finding pthread setname style - Mac OS X")
  elseif(PTHREAD_SETNAME_NETBSD)
    set(${style_var} NETBSD PARENT_SCOPE)
    set(${pthread_np_var} "TRUE" PARENT_SCOPE)
    message(STATUS "Finding pthread setname style - NetBSD")
  else()
    set(${style_var} "" PARENT_SCOPE)
    set(${pthread_np_var} "" PARENT_SCOPE)
    message(STATUS "Finding pthread setname style - unknown")
  endif()
endfunction()
