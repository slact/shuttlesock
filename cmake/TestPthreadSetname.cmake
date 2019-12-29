include(CheckCSourceCompiles)
include(CMakePushCheckState)

function(test_pthread_setname thread_libs style_var pthread_np_var)
  message(STATUS "Detecting pthread setname style")
  
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED QUIET)
  
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
    message(STATUS "Detecting pthread setname style - done. It's Linux")
  elseif(PTHREAD_SETNAME_OSX)
    set(setname_style OSX)
    set(pthread_np "")
    message(STATUS "Detecting pthread setname style - done. It's Mac OS X")
  elseif(PTRHEAD_SETNAME_FREEBSD)
    set(setname_style FREEBSD)
    set(pthread_np TRUE)
    message(STATUS "Detecting pthread setname style - done. It's FreeBSD")
  elseif(PTHREAD_SETNAME_NETBSD)
    set(setname_style NETBSD)
    set(pthread_np TRUE)
    message(STATUS "Detecting pthread setname style - done. It's NetBSD")
  else()
    set(setname_style "")
    set(pthread_np "")
    message(STATUS "Detecting pthread setname style - done. It's not available")
  endif()
  unset(PTHREAD_SETNAME_LINUX CACHE)
  unset(PTHREAD_SETNAME_OSX CACHE)
  unset(PTRHEAD_SETNAME_FREEBSD CACHE)
  unset(PTHREAD_SETNAME_NETBSD CACHE)
  
  set(${style_var} ${setname_style} PARENT_SCOPE)
  set(${pthread_np_var} ${pthread_np} PARENT_SCOPE)
endfunction()
