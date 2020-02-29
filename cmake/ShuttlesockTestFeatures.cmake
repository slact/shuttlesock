include(CMakeParseArguments)
include(CheckSymbolExists)
include(CMakePushCheckState)

function(shuttlesock_test_features)
  set(results
    TYPEOF_MACRO
    TYPE_GENERIC_MACROS
    BIG_ENDIAN
    EVENTFD
    IO_URING
    PTHREAD_SETNAME
    PTHREAD_NP_REQUIRED
    SO_REUSEPORT
    IPV6
    ACCEPT4
    STRSIGNAL
    HYPERSCAN
  )
  set(conditions
    USE_EVENTFD
    USE_IO_URING
    USE_HYPERSCAN
  )
  
  cmake_parse_arguments(TEST "" "" "CONDITIONS;RESULTS" ${ARGN})
  cmake_parse_arguments(CONDITION "" "${conditions}" "" ${TEST_CONDITIONS})
  cmake_parse_arguments(RESULT "" "${results}" "" ${TEST_RESULTS})

  
  foreach(check IN ITEMS result condition)
    foreach(ITEM IN LISTS "${check}s")
      string(TOUPPER ${check} CHECK)
      if("${${CHECK}_${ITEM}}" STREQUAL "")
        message(FATAL_ERROR "Missing feature ${check} variable ${CHECK}_${ITEM}")
      endif()
    endforeach()
  endforeach()
  
  #typeof() check
  if(NOT DEFINED ${RESULT_TYPEOF_MACRO})
    include(TestTypeofKeyword)
    test_typeof_keyword(have_typeof_macro)
    set(${RESULT_TYPEOF_MACRO} ${have_typeof_macro} CACHE INTERNAL "have typeof() macro")
  endif()

  # _Generic macro check
  if(NOT DEFINED ${RESULT_TYPE_GENERIC_MACROS})
    include(TestTypeGenericMacros)
    test_type_generic_macros(type_generic_macros)
    if(NOT type_generic_macros)
      message(FATAL_ERROR "Shuttlesock requires the compiler to support the _Generic macro. Yours doesn't.")
    endif()
    set(${RESULT_TYPE_GENERIC_MACROS} "TRUE" CACHE INTERNAL "have _Generic macros")
  endif()

  #endianness
  if(NOT DEFINED ${RESULT_BIG_ENDIAN})
    include(TestEndianness)
    test_machine_is_big_endian(is_big_endian)
    set(${RESULT_BIG_ENDIAN} ${is_big_endian} CACHE INTERNAL "Is this machine big endian?")
  endif()
  
  #eventfd
  if(NOT DEFINED ${RESULT_EVENTFD} OR (NOT "${${CONDITION_USE_EVENTFD}}" STREQUAL "${TEST_FEATURES_USE_EVENTFD_PREVIOUS_VALUE}"))
    set(TEST_FEATURES_USE_EVENTFD_PREVIOUS_VALUE "${${CONDITION_USE_EVENTFD}}" CACHE INTERNAL "")
    if(("${${CONDITION_USE_EVENTFD}}" STREQUAL "") OR "${${CONDITION_USE_EVENTFD}}")
      message(STATUS "Check if system supports eventfd")
      cmake_push_check_state(RESET)
      set(CMAKE_REQUIRED_QUIET ON)
      check_symbol_exists(eventfd "sys/eventfd.h" have_eventfd)
      if(NOT have_eventfd)
        message(STATUS "Check if system supports eventfd - no")
        if("${CONDITION_USE_EVENTFD}" STREQUAL "FORCE")
          message(FATAL_ERROR "Shuttlesock was configured to force usage of eventfd, but it is not present on this system")
        endif()
        set(${RESULT_EVENTFD} "FALSE" CACHE INTERNAL "use eventfd" FORCE)
      else()
        message(STATUS "Check if system supports eventfd - yes")
        set(${RESULT_EVENTFD} "TRUE" CACHE INTERNAL "use eventfd" FORCE)
      endif()
      cmake_reset_check_state()
    else()
      message(STATUS "Don't bother checking if system supports eventfd")
      set(${RESULT_EVENTFD} "FALSE" CACHE INTERNAL "use eventfd" FORCE)
    endif()
  endif()
  
  #io_uring support
  if(NOT DEFINED ${RESULT_IO_URING} OR (NOT "${${CONDITION_USE_IO_URING}}" STREQUAL "${TEST_FEATURES_USE_IO_URING_PREVIOUS_VALUE}"))
    set(TEST_FEATURES_USE_IO_URING_PREVIOUS_VALUE "${${CONDITION_USE_IO_URING}}" CACHE INTERNAL "")
    if(("${${CONDITION_USE_IO_URING}}" STREQUAL "") OR "${${CONDITION_USE_IO_URING}}")
      include(TestIoUringHeaders)
      test_io_uring_buildable(have_io_uring)
      set(${RESULT_IO_URING} "${have_io_uring}" CACHE INTERNAL "have io_uring")
      if("${${CONDITION_USE_IO_URING}}" STREQUAL "FORCE" AND NOT "${RESULT_IO_URING}")
        message(FATAL_ERROR "Shuttlesock was configuired to force usage of io_uring, but it is not supported on this system")
      endif()
    else()
      message(STATUS "Don't bother checking if system supports io_uring")
      set(${RESULT_IO_URING} "FALSE" CACHE INTERNAL "use io_uring" FORCE)
    endif()
  endif()

  #pthread_setname
  if(NOT DEFINED ${RESULT_PTHREAD_SETNAME})
    include(TestPthreadSetname)
    test_pthread_setname("${CMAKE_THREAD_LIBS_INIT}"
      pthread_setname_style
      pthread_setname_np_required
    )
    set(${RESULT_PTHREAD_SETNAME} ${pthread_setname_style} CACHE INTERNAL "pthread_setname function style")
    set(${RESULT_PTHREAD_NP_REQUIRED} ${pthread_setname_np_required} CACHE INTERNAL "is pthread_np.h needed for pthread_setname?")
    
  endif()

  #SO_REUSEPORT
  if(NOT DEFINED ${RESULT_SO_REUSEPORT})
    include(TestSO_REUSEPORT)
    #I'd rather use DEFINED CACHE{$var}, but that only got added in 3.14
    test_SO_REUSEPORT(have_so_reuseport)
    if(NOT have_so_reuseport)
      message(FATAL_ERROR "Shuttlesock requires SO_REUSEPORT support. Unfortunately this system doesn't have it.")
    else()
      set(${RESULT_SO_REUSEPORT} TRUE CACHE INTERNAL "Have SO_REUSEPORT on this system" FORCE)
    endif()
  endif()

  #IPv6
  if(NOT DEFINED ${RESULT_IPV6})
    include(TestIPv6)
    test_ipv6(have_ipv6)
    set(${RESULT_IPV6} ${have_ipv6} CACHE INTERNAL "system supports IPv6")
  endif()

  #accept4()
  if(NOT DEFINED ${RESULT_ACCEPT4})
    include(TestAccept4)
    test_accept4(have_accept4)
    set(${RESULT_ACCEPT4} ${have_accept4} CACHE INTERNAL "system supports accept4")
  endif()
  
  #strsignal()
  if(NOT DEFINED ${RESULT_STRSIGNAL})
    include(TestStrsignal)
    test_strsignal(have_strsignal)
    set(${RESULT_STRSIGNAL} ${have_strsignal} CACHE INTERNAL "system has strsignal()")
  endif()
  
  
  #can we use hyperscan? Only if it's an x86 or x86-64 CPU
  if(NOT DEFINED ${RESULT_HYPERSCAN} OR (NOT "${${CONDITION_USE_HYPERSCAN}}" STREQUAL "${TEST_FEATURES_USE_HYPERSCAN_PREVIOUS_VALUE}"))
    set(TEST_FEATURES_USE_HYPERSCAN_PREVIOUS_VALUE "${${CONDITION_USE_HYPERSCAN}}" CACHE INTERNAL "")
    if(("${${CONDITION_USE_HYPERSCAN}}" STREQUAL "") OR "${${CONDITION_USE_HYPERSCAN}}")
      message(STATUS "Check if system is x86 or x86_64 for Hyperscan")
      include(TargetArch)
      target_architecture(target_arch)
      if(target_arch STREQUAL "x86_64" OR target_arch STREQUAL "i386")
        set(on_x86 YES)
        message(STATUS "Check if system is x86 or x86_64 for Hyperscan - yes (${target_arch})")
      else()
        message(STATUS "Check if system is x86 or x86_64 for Hyperscan - no (${target_arch})")
      endif()
      set(${RESULT_HYPERSCAN} "${on_x86}" CACHE INTERNAL "hyperscan can be used")
      if("${${CONDITION_USE_HYPERSCAN}}" STREQUAL "FORCE" AND NOT "${RESULT_HYPERSCAN}")
        message(FATAL_ERROR "Shuttlesock was configuired to force usage of hyperscan, but it is not supported on this system")
      endif()
    else()
      message(STATUS "Don't bother checking if system supports hyperscan")
      set(${RESULT_HYPERSCAN} "FALSE" CACHE INTERNAL "hyperscan can be used" FORCE)
    endif()
  endif()
endfunction()
