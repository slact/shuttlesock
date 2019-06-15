include(CheckCSourceCompiles)
include(CMakePushCheckState)

function(test_pthread_setname thread_libs style_var pthread_np_var)
  if(DEFINED ${style_var} AND DEFINED ${pthread_np_var})
    #I'd rather use DEFINED CACHE{$var}, but that only got added in 3.14
    return()
  endif()
  message(STATUS "Finding pthread setname style")
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_QUIET 1)
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
    set(setname_style LINUX)
    set(pthread_np "")
    message(STATUS "Finding pthread setname style - Linux")
  elseif(PTHREAD_SETNAME_OSX)
    set(setname_style OSX)
    set(pthread_np "")
    message(STATUS "Finding pthread setname style - Mac OS X")
  elseif(PTRHEAD_SETNAME_FREEBSD)
    set(setname_style FREEBSD)
    set(pthread_np TRUE)
    message(STATUS "Finding pthread setname style - FreeBSD")
  elseif(PTHREAD_SETNAME_NETBSD)
    set(setname_style NETBSD)
    set(pthread_np TRUE)
    message(STATUS "Finding pthread setname style - NetBSD")
  else()
    set(setname_style "")
    set(pthread_np "")
    message(STATUS "Finding pthread setname style - unknown")
  endif()
  
  set(${style_var} ${setname_style} CACHE INTERNAL "pthread_setname function style" FORCE)
  set(${pthread_np_var} ${pthread_np} CACHE INTERNAL "is pthread_np.h needed for pthread_setname?" FORCE)
endfunction()
