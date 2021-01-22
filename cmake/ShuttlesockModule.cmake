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

set(SHUTTLESOCK_CORE_MODULES_LISTING "" CACHE INTERNAL "Core modules listings for neat C iteration" FORCE)
set(SHUTTLESOCK_CORE_MODULES_INCLUDE_HEADERS "" CACHE INTERNAL "Module headers list" FORCE)
set(SHUTTLESOCK_CORE_MODULES_PREINITIALIZATION_FUNCTIONS "" CACHE INTERNAL "Core module preinitialization functions" FORCE)
set(SHUTTLESOCK_CORE_MODULES_FINISHED "" CACHE INTERNAL "done with core modules" FORCE)

function(shuttlesock_generate_core_modules_build_files)
  list(LENGTH SHUTTLESOCK_CORE_MODULES SHUTTLESOCK_CORE_MODULES_LENGTH)
  string(REPLACE ";" "\n" SHUTTLESOCK_CORE_MODULES_INCLUDE_HEADERS "${SHUTTLESOCK_CORE_MODULES_INCLUDE_HEADERS}")
  
  string(REPLACE ";" ",\n  " SHUTTLESOCK_CORE_MODULES_LISTING "${SHUTTLESOCK_CORE_MODULES_LISTING}")
  
  configure_file(src/include/shuttlesock/core_modules.h.tmpl src/include/shuttlesock/core_modules.h)
  configure_file(src/core_modules.c.tmpl src/core_modules.c)
  target_sources(shuttlesock PRIVATE src/core_modules.c)
  set(SHUTTLESOCK_CORE_MODULES_FINISHED "TRUE" CACHE INTERNAL "done with core modules" FORCE)
endfunction()

function(shuttlesock_add_module MODULE_NAME)
  cmake_parse_arguments(MODULE "LUA;CORE" "PREPARE_FUNCTION;LUA_REQUIRE;EXECUTABLE;EXECUTABLE_LUA;EXECUTABLE_SOURCE;EXECUTABLE_LUA_SOURCE" "SOURCES;HEADERS;LUA_SOURCES;LUA_MODULES" ${ARGN})
  
  list(APPEND MODULE_SOURCES ${MODULE_UNPARSED_ARGUMENTS})
  list(LENGTH MODULE_SOURCES MODULE_SOURCES_LENGTH)
  list(LENGTH MODULE_LUA_MODULES LUA_MODULES_LENGTH)
  list(LENGTH MODULE_LUA_SOURCES LUA_SOURCES_LENGTH)
  math(EXPR EVERYTHING_LENGTH "${MODULE_SOURCES_LENGTH} + ${LUA_MODULES_LENGTH} + ${LUA_SOURCES_LENGTH}")
  if(EVERYTHING_LENGTH EQUAL 0)
    message(FATAL_ERROR "module ${MODULE_NAME} has no sources")
  endif()
  
  if(MODULE_CORE)
    set(SHUTTLESOCK_MODULES_LIST_NAME SHUTTLESOCK_CORE_MODULES)
    set(SHUTTLESOCK_MODULES_HEADERS_LIST_NAME SHUTTLESOCK_CORE_MODULE_HEADERS)
    set(MODULE_SOURCES_PATH "src/modules/${MODULE_NAME}")
    
  else()
    set(SHUTTLESOCK_MODULES_LIST_NAME SHUTTLESOCK_ADDON_MODULES)
    set(SHUTTLESOCK_MODULES_HEADERS_LIST_NAME SHUTTLESOCK_ADDON_MODULE_HEADERS)
    if(MODULE_PATH)
      set(MODULE_SOURCES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${MODULE_PATH}")
    else()
      set(MODULE_SOURCES_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
  endif()
  
  list(APPEND ${SHUTTLESOCK_MODULES_LIST_NAME} "${MODULE_NAME}")
  set(${SHUTTLESOCK_MODULES_LIST_NAME} "${${SHUTTLESOCK_MODULES_LIST_NAME}}" CACHE INTERNAL "Modules list" FORCE)
  
  set(module_public_headers "")
  set(core_module_public_headers "")
  set(core_module_all_headers "")
  set(module_pathed_sources "")
  
  foreach(HEADER IN LISTS MODULE_HEADERS)
    if(HEADER STREQUAL "PRIVATE")
      set(private_header TRUE)
    else()
      set(header_file "${MODULE_SOURCES_PATH}/${HEADER}")
      set(dst_header_file "src/include/shuttlesock/modules/${MODULE_NAME}/${HEADER}")
      if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${header_file}")
        message(FATAL_ERROR "module ${MODULE_NAME} has no header file ${header_file}")
      endif()
      
      list(APPEND module_public_headers "${MODULE_NAME}/${HEADER}")
      
      get_filename_component(dst_header_path "${dst_header_file}" DIRECTORY)
      file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${dst_header_path}")
      if("${MODULE_CORE}")
        list(APPEND core_module_all_headers "${MODULE_NAME}/${HEADER}")
        if(NOT "${private_header}")
          list(APPEND core_module_public_headers "${MODULE_NAME}/${HEADER}")
          list(APPEND SHUTTLESOCK_CORE_MODULES_INCLUDE_HEADERS "#include <shuttlesock/modules/${MODULE_NAME}/${HEADER}>")
        endif()
      endif()
      set(private_header FALSE)
    endif()
  endforeach()
  set(SHUTTLESOCK_CORE_MODULES_INCLUDE_HEADERS "${SHUTTLESOCK_CORE_MODULES_INCLUDE_HEADERS}" CACHE INTERNAL "Core modules default includes" FORCE)
  
  foreach(SRC IN LISTS MODULE_SOURCES)
    list(APPEND module_pathed_sources "${MODULE_SOURCES_PATH}/${SRC}")
  endforeach()
  
  math(EXPR LUA_MODULES_HALFLENGTH "${LUA_MODULES_LENGTH} / 2")
  if(LUA_MODULES_HALFLENGTH MATCHES "\\D")
    message(FATAL_ERROR "failed to add module ${MODULE_NAME}: odd number of LUA_MODULES parameters")
  endif()
  if(LUA_MODULES_LENGTH GREATER 0)
    math(EXPR rangemax "${LUA_MODULES_LENGTH} - 1")
    foreach(index RANGE 0 ${rangemax} 2)
      math(EXPR index1 "${index}+1")
      list(GET MODULE_LUA_MODULES "${index}" lmname)
      list(GET MODULE_LUA_MODULES "${index1}" lmfile)
      pack_lua_module("${lmname}" "${MODULE_SOURCES_PATH}/${lmfile}")
    endforeach()
  endif()
  
  if(LUA_SOURCES_LENGTH GREATER 0)
    math(EXPR rangemax "${LUA_SOURCES_LENGTH} - 1")
    foreach(index RANGE 0 ${rangemax} 2)
      math(EXPR index1 "${index}+1")
      list(GET MODULE_LUA_SOURCES "${index}" lsname)
      list(GET MODULE_LUA_SOURCES "${index1}" lsfile)
      pack_lua_script("${lsname}" "${MODULE_SOURCES_PATH}/${lsfile}")
    endforeach()
  endif()
  
  if(MODULE_LUA)
    set(module_ptr NULL)
    set(MODULE_LUA_BOOL true)
  else()
    set(module_ptr "&shuso_${MODULE_NAME}_module")
    set(MODULE_LUA_BOOL false)
  endif()
  if(MODULE_LUA_REQUIRE)
    set(MODULE_LUA_REQUIRE "\"${MODULE_LUA_REQUIRE}\"")
  else()
    set(MODULE_LUA_REQUIRE NULL)
  endif()
  if(MODULE_LUA_RUN_SCRIPT)
    set(MODULE_LUA_RUN_SCRIPT "\"${MODULE_LUA_RUN_SCRIPT}\"")
  else()
    set(MODULE_LUA_RUN_SCRIPT NULL)
  endif()
  if(MODULE_PREPARE_FUNCTION)
    set(MODULE_PREPARE_FUNCTION "&${MODULE_PREPARE_FUNCTION}")
  else()
    set(MODULE_PREPARE_FUNCTION NULL)
  endif()
  
  set(module_listing "{.name=\"${MODULE_NAME}\", .module=${module_ptr}, .lua_module=${MODULE_LUA_BOOL}, .lua_require=${MODULE_LUA_REQUIRE}, .lua_script=${MODULE_LUA_RUN_SCRIPT}, .prepare_function=${MODULE_PREPARE_FUNCTION}}")
  
  if(MODULE_CORE)
    target_sources(shuttlesock PRIVATE ${module_pathed_sources})
      
    list(APPEND SHUTTLESOCK_CORE_MODULES_LISTING "${module_listing}")
    set(SHUTTLESOCK_CORE_MODULES_LISTING "${SHUTTLESOCK_CORE_MODULES_LISTING}" CACHE INTERNAL "" FORCE)
    
    set(symlink_commands "")
    set(symlink_byproducts "")
    
    foreach(header IN LISTS core_module_all_headers)
      set(target "${CMAKE_CURRENT_LIST_DIR}/src/modules/${header}")
      set(link "${CMAKE_CURRENT_BINARY_DIR}/src/include/shuttlesock/modules/${header}")
      list(APPEND symlink_commands
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${target} ${link}
      )
      list(APPEND symlink_byproducts ${link})
    endforeach()
    
    #set(SHUTTLESOCK_CORE_MODULES_POINTERS "${SHUTTLESOCK_CORE_MODULES_POINTERS}  &shuso_${MODULE_NAME}_module,\n" CACHE INTERNAL "Core module pointers" FORCE)
    
    set(targetname "shuttlesock_symlink_${MODULE_NAME}_module_headers")
    add_custom_target("${targetname}" ALL
      ${symlink_commands}
      BYPRODUCTS
      ${symlink_byproducts}
    )
    add_dependencies(shuttlesock "${targetname}")
    
    
    #refinish core modules
    #inefficient to do this for every core module, but cleaner CMakefile as a result
    shuttlesock_generate_core_modules_build_files()
  else()
    add_library("shuttlesock_module_${MODULE_NAME}" SHARED)
    set_property(TARGET "shuttlesock_module_${MODULE_NAME}" PROPERTY C_STANDARD 11)
    set_property(TARGET "shuttlesock_module_${MODULE_NAME}" PROPERTY POSITION_INDEPENDENT_CODE 1)
    target_sources("shuttlesock_module_${MODULE_NAME}" PRIVATE ${module_pathed_sources})
  endif()
endfunction()

