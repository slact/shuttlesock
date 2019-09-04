include(TestOverlengthStrings)

set(GLOBAL_COMPILE_FLAGS "")
set(GLOBAL_LINK_FLAGS "")

macro(add_compiler_flags)
  add_compile_options(${ARGV})
  set(GLOBAL_COMPILE_FLAGS ${GLOBAL_COMPILE_FLAGS} ${ARGV})
endmacro()

function(get_compiler_flags flags_var)
  string (REPLACE ";" " " _FLAGS "${GLOBAL_COMPILE_FLAGS}")
  set(${flags_var} ${_FLAGS} PARENT_SCOPE)
endfunction()

macro(add_linker_flags)
  string (REPLACE ";" " " _FLAGS "${ARGV}")
  set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${_FLAGS}")
  set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${_FLAGS}")
  set (CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${_FLAGS}")
  set (GLOBAL_LINK_FLAGS "${GLOBAL_LINK_FLAGS} ${_FLAGS}")
endmacro()

function(get_linker_flags flags_var)
  set(${flags_var} ${GLOBAL_LINK_FLAGS} PARENT_SCOPE)
endfunction()

function(add_build_mode mode cflags linker_flags)
  string(TOUPPER ${mode} MODE)
  set(CMAKE_CXX_FLAGS_${MODE} "${cflags}" CACHE STRING "C++ flags for build mode ${mode}" FORCE)
  set(CMAKE_C_FLAGS_${MODE} "${cflags}"  CACHE STRING "C flags for build mode ${mode}" FORCE)
  set(CMAKE_SHARED_LINKER_FLAGS_${MODE} "${linker_flags}"  CACHE STRING "Linker flags for build mode ${mode}" FORCE)
  mark_as_advanced(
    CMAKE_CXX_FLAGS_${MODE}
    CMAKE_C_FLAGS_${MODE}
    CMAKE_EXE_LINKER_FLAGS_${MODE}
    CMAKE_SHARED_LINKER_FLAGS_${MODE}
  )
endfunction()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

#if("${CMAKE_C_COMPILER_ID}" STREQUAL "AppleClang")
  set(link_ubsan "")
  set(leak_sanitizer "")
#else()
#  set(link_ubsan "-lubsan")
#  set(leak_sanitizer "-fsanitize=leak")
#endif()

set(msan_blacklist ${CMAKE_CURRENT_SOURCE_DIR}/memory-sanitizer-blacklist.txt)

add_build_mode(DebugMSan 
  "-fsanitize=memory -fsanitize-memory-track-origins=2 ${leak_sanitizer} -fsanitize-blacklist=${msan_blacklist}"
  "-fsanitize=memory -fsanitize-blacklist=${msan_blacklist} -fsanitize-memory-track-origins=2 ${link_ubsan}"
)
add_build_mode(DebugTSan
  "-fsanitize=thread -fsanitize=undefined"
  "-fsanitize=thread -fsanitize=undefined ${link_ubsan}"
)

if("${CMAKE_C_COMPILER_ID}" MATCHES "^(Apple)?Clang$")
  add_build_mode(DebugCoverage
    "-fprofile-instr-generate -fcoverage-mapping"
    "-fprofile-instr-generate -fcoverage-mapping"
  )
else()
  add_build_mode(DebugCoverage
    "-fprofile-arcs -ftest-coverage"
    "-fprofile-arcs -ftest-coverage"
  )
endif()

if(CMAKE_BUILD_TYPE MATCHES "^Debug")
  if(NOT OPTIMIZE_LEVEL)
    set(OPTIMIZE_LEVEL 0)
  endif()
  
  add_compiler_flags(-Wall -Wextra -Wpedantic -Wno-unused-parameter -Wpointer-sign -Wpointer-arith -Wshadow -Wnested-externs -Wsign-compare -ggdb -O${OPTIMIZE_LEVEL} -fno-omit-frame-pointer)
  if ("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
    add_compiler_flags(-fdiagnostics-color=always -Wmaybe-uninitialized -fvar-tracking-assignments)
  elseif("${CMAKE_C_COMPILER_ID}" MATCHES "^(Apple)?Clang$")
    add_compiler_flags()
  endif()
endif()


set( CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}" CACHE 
  STRING
    "Choose the type of build, options are: Debug DebugASan DebugMSan DebugTSan DebugCoverageGCC DebugCoverageClang Release RelWithDebInfo MinSizeRel."
  FORCE
)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM AND NOT DISABLE_CCACHE)
  set(CMAKE_C_COMPILER_LAUNCHER  ${CCACHE_PROGRAM})
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

if(NOT DEFINED C_COMPILER_WARNS_ON_OVERLENGTH_STRINGS)
  test_overlength_strings(C_COMPILER_WARNS_ON_OVERLENGTH_STRINGS)
  set(C_COMPILER_WARNS_ON_OVERLENGTH_STRINGS ${C_COMPILER_WARNS_ON_OVERLENGTH_STRINGS} CACHE BOOL "-Wno-overlength-strings supported")
endif()
if(C_COMPILER_WARNS_ON_OVERLENGTH_STRINGS)
  add_compiler_flags(-Wno-overlength-strings)
endif()
