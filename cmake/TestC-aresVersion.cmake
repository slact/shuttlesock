include(CheckCSourceRuns)
include(CMakePushCheckState)

function(test_c_ares_version_min c_ares_version_check_result_var ver_major ver_minor ver_patch)
  set(c_ares_var "C_ARES_VERSION_MIN_${ver_major}_${ver_minor}_${ver_patch}")
  if(DEFINED CACHE{${c_ares_var})
    return()
  endif()
  message(STATUS "Check if c-ares version >= ${ver_major}.${ver_minor}.${ver_patch}")
  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_QUIET 1)
  check_c_source_runs("
    #include <ares_version.h>
    int main(void) {
      return !(ARES_VERSION_MAJOR >= ${ver_major} && ARES_VERSION_MINOR >= ${ver_minor} && ARES_VERSION_PATCH >= ${ver_patch});
    }
  " "${c_ares_var}")
  cmake_reset_check_state()
  if("${${c_ares_var}}")
    message(STATUS "Check if c-ares version >= ${ver_major}.${ver_minor}.${ver_patch} - yes")
    set(${c_ares_version_check_result_var} "YES" PARENT_SCOPE)
  else()
    message(STATUS "Check if c-ares version >= ${ver_major}.${ver_minor}.${ver_patch} - no")
    set(${c_ares_version_check_result_var} "NO" PARENT_SCOPE)
  endif()
endfunction()
