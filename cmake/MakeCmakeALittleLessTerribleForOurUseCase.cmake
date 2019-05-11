macro(add_compiler_flags)
  add_compile_options(${ARGV})
endmacro()
macro(set_compiler_flags)

endmacro()
macro(add_linker_flags)
  string (REPLACE ";" " " _FLAGS "${ARGV}")
  set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${_FLAGS}")
  set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${_FLAGS}")
  set (CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${_FLAGS}")
endmacro()

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

#add DebugASan mode
add_build_mode(DebugASan 
  "-fsanitize-address-use-after-scope -fsanitize=address -fsanitize=undefined  -fsanitize=leak"
  "-fsanitize=address -fsanitize=undefined -lubsan"
)
add_build_mode(DebugMSan 
  "-fsanitize=memory -fsanitize=undefined"
  "-fsanitize=memory -fsanitize=undefined -lubsan"
)
add_build_mode(DebugTSan
  "-fsanitize=thread -fsanitize=undefined"
  "-fsanitize=thread -fsanitize=undefined -lubsan"
)
add_build_mode(DebugCoverageGCC
  "-fprofile-arcs -ftest-coverage"
  "-fprofile-arcs -ftest-coverage"
)
add_build_mode(DebugCoverageClang
  "-fprofile-instr-generate -fcoverage-mapping"
  "-fprofile-instr-generate -fcoverage-mapping"
)

if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
  add_build_mode(DebugCoverage
    "-fprofile-arcs -ftest-coverage"
    "-fprofile-arcs -ftest-coverage"
  )
elseif("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
  add_build_mode(DebugCoverage
    "-fprofile-instr-generate -fcoverage-mapping"
    "-fprofile-instr-generate -fcoverage-mapping"
  )
else()
  message(FATAL_ERROR "Don't know how to generate coverage for \"${CMAKE_C_COMPILER_ID}\" compiler")
endif()

if(CMAKE_BUILD_TYPE MATCHES "^Debug")
  if(NOT OPTIMIZE_LEVEL)
    set(OPTIMIZE_LEVEL 0)
  endif()
  
  add_compiler_flags(-Wall -Wextra -pedantic -Wno-unused-parameter -Wpointer-sign -Wpointer-arith -Wshadow -Wnested-externs -Wsign-compare -ggdb -O${OPTIMIZE_LEVEL})
  if ("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
    add_compiler_flags(-fdiagnostics-color=always -Wmaybe-uninitialized -fvar-tracking-assignments -Wimplicit-fallthrough)
  endif()
endif()


set( CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}" CACHE 
  STRING
    "Choose the type of build, options are: Debug DebugASan DebugMSan DebugTSan DebugCoverageGCC DebugCoverageClang Release RelWithDebInfo MinSizeRel."
  FORCE
)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER  ${CCACHE_PROGRAM})
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()
