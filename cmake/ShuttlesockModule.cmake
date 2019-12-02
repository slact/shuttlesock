include(CMakeParseArguments)

#function(include_shuttlesock_module module_path SHUTTLESOCK_INCLUDED_MODULE_NAME)
#  add_subdirectory("${module_path}")
#  add_library("shuttlesock_${SHUTTLESOCK_INCLUDED_MODULE_NAME}" SHARED)
#endfunction()
#
#macro(shuttlesock_module SHUTTLESOCK_MODULE_NAME)
#  cmake_parse_arguments(SHUTTLESOCK_MODULE "" "LANGUAGE;VERSION;DESCRIPTION" "SOURCE;INCLUDE" ${ARGN})
#  set(SHUTTLESOCK_MODULE_LIBRARY "shuttlesock_${SHUTTLESOCK_MODULE_NAME}")
#  project("${SHUTTLESOCK_MODULE_LIBRARY}" LANGUAGES C VERSION ${SHUTTLESOCK_MODULE_VERSION})
  
#  set(DESCRIPTION "${SHUTTLESOCK_MODULE_DESCRIPTION}")
#  target_link_libraries("${SHUTTLESOCK_MODULE_LIBRARY}" PRIVATE shuttlesock)
#  add_library(${SHUTTLESOCK_MODULE_LIBRARY} ${SHUTTLESOCK_MODULE_SOURCE})
#endmacro()

set(SHUTTLESOCK_CORE_MODULE_HEADERS "" CACHE INTERNAL "Core module headers" FORCE)
set(SHUTTLESOCK_CORE_MODULES "" CACHE INTERNAL "Core modules" FORCE)
set(SHUTTLESOCK_CORE_LUA_MODULES "" CACHE INTERNAL "Core Lua modules" FORCE)
set(SHUTTLESOCK_CORE_MODULE_PREINITIALIZATION_FUNCTIONS "" CACHE INTERNAL "Core module preinitialization functions" FORCE)
set(SHUTTLESOCK_CORE_MODULE_INCLUDE_HEADERS "" CACHE INTERNAL "Core module headers" FORCE)
set(SHUTTLESOCK_CORE_MODULES_LIST "" CACHE INTERNAL "Core module listings" FORCE)

function(add_core_module MODULE_NAME)
  cmake_parse_arguments(CORE_MODULE "LUA" "PREPARE_FUNCTION;LUA_REQUIRE" "SOURCES;HEADERS;LUA_SOURCES;LUA_MODULES" ${ARGN})
  list(APPEND CORE_MODULE_SOURCES ${CORE_MODULE_UNPARSED_ARGUMENTS})
  list(LENGTH CORE_MODULE_SOURCES CORE_MODULE_SOURCES_LENGTH)
  list(LENGTH CORE_MODULE_LUA_MODULES LUA_MODULES_LENGTH)
  list(LENGTH CORE_MODULE_LUA_SOURCES LUA_SOURCES_LENGTH)
  math(EXPR EVERYTHING_LENGTH "${CORE_MODULE_SOURCES_LENGTH} + ${LUA_MODULES_LENGTH} + ${LUA_SOURCES_LENGTH}")
  if(EVERYTHING_LENGTH EQUAL 0)
    message(FATAL_ERROR "module ${MODULE_NAME} has no sources")
  endif()
  
  foreach(CORE_SRC IN LISTS CORE_MODULE_SOURCES)
    list(APPEND CORE_PATHED_SOURCES "src/modules/${CORE_SRC}")
  endforeach()
  
  list(APPEND SHUTTLESOCK_CORE_MODULES "${MODULE_NAME}")
  set(SHUTTLESOCK_CORE_MODULES "${SHUTTLESOCK_CORE_MODULES}" CACHE INTERNAL "Core modules" FORCE)
  
  foreach(CORE_HEADER IN LISTS CORE_MODULE_HEADERS)
    if(CORE_HEADER STREQUAL "PRIVATE")
      set(private_header TRUE)
    else()
      set(header_file "src/modules/${CORE_HEADER}")
      list(APPEND pathed_core_header_files ${header_file})
      set(dst_header_file "src/include/shuttlesock/modules/${CORE_HEADER}")
      if(NOT EXISTS "${header_file}")
        message(FATAL_ERROR "module ${MODULE_NAME} has no header file ${header_file}")
      endif()
      list(APPEND SHUTTLESOCK_CORE_MODULE_HEADERS "${CORE_HEADER}")
      set(SHUTTLESOCK_CORE_MODULE_HEADERS "${SHUTTLESOCK_CORE_MODULE_HEADERS}" CACHE INTERNAL "Core module headers" FORCE)
      get_filename_component(dst_header_path "${dst_header_file}" DIRECTORY)
      file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${dst_header_path}")
      if(NOT private_header)
        list(APPEND SHUTTLESOCK_CORE_MODULE_INCLUDE_HEADERS "${CORE_HEADER}")
        set(SHUTTLESOCK_CORE_MODULE_INCLUDE_HEADERS "${SHUTTLESOCK_CORE_MODULE_INCLUDE_HEADERS}" CACHE INTERNAL "Core module headers" FORCE)
      endif()
      set(private_header FALSE)
    endif()
  endforeach()
  
  target_sources(shuttlesock PRIVATE ${CORE_PATHED_SOURCES})
  
  math(EXPR LUA_MODULES_HALFLENGTH "${LUA_MODULES_LENGTH} / 2")
  if(LUA_MODULES_HALFLENGTH MATCHES "\\D")
    message(FATAL_ERROR "failed to add module ${MODULE_NAME}: odd number of LUA_MODULES parameters")
  endif()
  if(LUA_MODULES_LENGTH GREATER 0)
    math(EXPR rangemax "${LUA_MODULES_LENGTH} - 1")
    foreach(index RANGE 0 ${rangemax} 2)
      math(EXPR index1 "${index}+1")
      list(GET CORE_MODULE_LUA_MODULES "${index}" lmname)
      list(GET CORE_MODULE_LUA_MODULES "${index1}" lmfile)
      pack_lua_module("${lmname}" "src/modules/${lmfile}")
    endforeach()
  endif()
  
  if(LUA_SOURCES_LENGTH GREATER 0)
    math(EXPR rangemax "${LUA_SOURCES_LENGTH} - 1")
    foreach(index RANGE 0 ${rangemax} 2)
      math(EXPR index1 "${index}+1")
      list(GET CORE_MODULE_LUA_SOURCES "${index}" lsname)
      list(GET CORE_MODULE_LUA_SOURCES "${index1}" lsfile)
      pack_lua_script("${lsname}" "src/modules/${lsfile}")
    endforeach()
  endif()
  
  if(CORE_MODULE_LUA)
    set(module_ptr NULL)
    set(CORE_MODULE_LUA_BOOL true)
  else()
    set(module_ptr "&shuso_${MODULE_NAME}_module")
    set(CORE_MODULE_LUA_BOOL false)
  endif()
  if(CORE_MODULE_LUA_REQUIRE)
    set(CORE_MODULE_LUA_REQUIRE "\"${CORE_MODULE_LUA_REQUIRE}\"")
  else()
    set(CORE_MODULE_LUA_REQUIRE NULL)
  endif()
  if(CORE_MODULE_LUA_RUN_SCRIPT)
    set(CORE_MODULE_LUA_RUN_SCRIPT "\"${CORE_MODULE_LUA_RUN_SCRIPT}\"")
  else()
    set(CORE_MODULE_LUA_RUN_SCRIPT NULL)
  endif()
  if(CORE_MODULE_PREPARE_FUNCTION)
    set(CORE_MODULE_PREPARE_FUNCTION "&${CORE_MODULE_PREPARE_FUNCTION}")
  else()
    set(CORE_MODULE_PREPARE_FUNCTION NULL)
  endif()
  
  set(module_listing "{.name=\"${MODULE_NAME}\", .module=${module_ptr}, .lua_module=${CORE_MODULE_LUA_BOOL}, .lua_require=${CORE_MODULE_LUA_REQUIRE}, .lua_script=${CORE_MODULE_LUA_RUN_SCRIPT}, .prepare_function=${CORE_MODULE_PREPARE_FUNCTION}}")
  list(APPEND SHUTTLESOCK_CORE_MODULES_LIST "${module_listing}")
  set(SHUTTLESOCK_CORE_MODULES_LIST "${SHUTTLESOCK_CORE_MODULES_LIST}" CACHE INTERNAL "Core module listings" FORCE)
endfunction()

function(finish_adding_core_modules)
  foreach(header IN LISTS SHUTTLESOCK_CORE_MODULE_HEADERS)
    set(target "${CMAKE_CURRENT_LIST_DIR}/src/modules/${header}")
    set(link "${CMAKE_CURRENT_BINARY_DIR}/src/include/shuttlesock/modules/${header}")
    list(APPEND symlink_commands
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${target} ${link}
    )
    list(APPEND symlink_byproducts ${link})
  endforeach()
  foreach(header IN LISTS SHUTTLESOCK_CORE_MODULE_INCLUDE_HEADERS)
    set(SHUTTLESOCK_CORE_MODULES_INCLUDES "${SHUTTLESOCK_CORE_MODULES_INCLUDES}#include <shuttlesock/modules/${header}>\n")
  endforeach()
  
  set(targetname "symlink_core_module_headers")
  add_custom_target("${targetname}" ALL
    ${symlink_commands}
    BYPRODUCTS
      ${symlink_byproducts}
  )
  add_dependencies(shuttlesock "${targetname}")
  
  list(LENGTH SHUTTLESOCK_CORE_MODULES SHUTTLESOCK_CORE_MODULES_LENGTH)
  
  foreach(MODULE IN LISTS SHUTTLESOCK_CORE_MODULES)
    set(SHUTTLESOCK_CORE_MODULES_POINTERS "${SHUTTLESOCK_CORE_MODULES_POINTERS}  &shuso_${MODULE}_module,\n")
  endforeach()
  
  
  foreach(LISTING IN LISTS SHUTTLESOCK_CORE_MODULES_LIST)
    set(SHUTTLESOCK_CORE_MODULE_LISTINGS "${SHUTTLESOCK_CORE_MODULE_LISTINGS}  ${LISTING},\n")
  endforeach()
  
  configure_file(src/include/shuttlesock/core_modules.h.tmpl src/include/shuttlesock/core_modules.h)
  configure_file(src/core_modules.c.tmpl src/core_modules.c)
  target_sources(shuttlesock PRIVATE src/core_modules.c)
  
endfunction()