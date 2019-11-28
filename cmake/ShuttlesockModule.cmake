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

function(add_static_module MODULE_NAME)
  cmake_parse_arguments(STATIC "" "" "SOURCES;HEADERS;LUA_SOURCES;LUA_MODULES" ${ARGN})
  list(APPEND STATIC_SOURCES ${STATIC_UNPARSED_ARGUMENTS})
  list(LENGTH STATIC_SOURCES STATIC_SOURCES_LENGTH)
  list(LENGTH STATIC_LUA_MODULES LUA_MODULES_LENGTH)
  list(LENGTH STATIC_LUA_SOURCES LUA_SOURCES_LENGTH)
  math(EXPR EVERYTHING_LENGTH "${STATIC_SOURCES_LENGTH} + ${LUA_MODULES_LENGTH} + ${LUA_SOURCES_LENGTH}")
  if(EVERYTHING_LENGTH EQUAL 0)
    message(FATAL_ERROR "module ${MODULE_NAME} has no sources")
  endif()
  
  foreach(STATIC_SRC IN LISTS STATIC_SOURCES)
    list(APPEND STATIC_PATHED_SOURCES "src/modules/${STATIC_SRC}")
  endforeach()
  
  if(NOT DEFINED SHUTTLESOCK_STATIC_MODULE_HEADERS)
    set(SHUTTLESOCK_STATIC_MODULE_HEADERS "" CACHE INTERNAL "Static module headers")
  endif()
  
  set(header_count 0)
  foreach(STATIC_HEADER IN LISTS STATIC_HEADERS)
    set(header_file "src/modules/${STATIC_HEADER}")
    set(dst_header_file "src/include/shuttlesock/modules/${STATIC_HEADER}")
    if(NOT EXISTS "${header_file}")
      message(FATAL_ERROR "module ${MODULE_NAME} has no header file ${header_file}")
    endif()
    list(APPEND SHUTTLESOCK_STATIC_MODULE_HEADERS "${STATIC_HEADER}")
    set(SHUTTLESOCK_STATIC_MODULE_HEADERS "${SHUTTLESOCK_STATIC_MODULE_HEADERS}" CACHE INTERNAL "Static module headers" FORCE)
    set(depname "link_static_module_${MODULE_NAME}_header_${header_count}")
    get_filename_component(dst_header_path "${dst_header_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${dst_header_path}")
  endforeach()
  
  target_sources(shuttlesock PRIVATE ${STATIC_PATHED_SOURCES})
  
  math(EXPR LUA_MODULES_HALFLENGTH "${LUA_MODULES_LENGTH} / 2")
  if(LUA_MODULES_HALFLENGTH MATCHES "\\D")
    message(FATAL_ERROR "failed to add module ${MODULE_NAME}: odd number of LUA_MODULES parameters")
  endif()
  foreach(index RANGE 0 ${LUA_MODULES_LENGTH} 2)
    math(EXPR index1 "${index}+1")
    list(GET "${STATIC_LUA_MODULES}" "${index}" lmname)
    list(GET "${STATIC_LUA_MODULES}" "${index1}" lmfile)
    pack_lua_module("shuttlesock.module.${lmname}" "src/modules/${lmpath}")
  endforeach()
  
  foreach(index RANGE 0 ${LUA_SOURCES_LENGTH} 2)
    math(EXPR index1 "${index}+1")
    list(GET "${STATIC_LUA_SOURCES}" "${index}" lsname)
    list(GET "${STATIC_LUA_SOURCES}" "${index1}" lsfile)
    pack_lua_script("shuttlesock.module.${lsname}" "src/modules/${lspath}")
  endforeach()
endfunction()

function(finish_adding_static_modules ALL_CORE_MODULES_HEADER)
  set(targetname "symlink_static_module_headers")
  foreach(header IN LISTS SHUTTLESOCK_STATIC_MODULE_HEADERS)
    set(target "${CMAKE_CURRENT_LIST_DIR}/src/modules/${header}")
    set(link "${CMAKE_CURRENT_BINARY_DIR}/src/include/shuttlesock/modules/${header}")
    list(APPEND symlink_commands
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${target} ${link}
    )
    list(APPEND symlink_byproducts ${link})
    set(core_modules_includes "${core_modules_includes}#include <shuttlesock/modules/${header}>\n")
  endforeach()
  add_custom_target("${targetname}" ALL
    ${symlink_commands}
    BYPRODUCTS
      ${symlink_byproducts}
  )
  add_dependencies(shuttlesock "${targetname}")
  
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${ALL_CORE_MODULES_HEADER}"
    "#ifndef SHUTTLESOCK_CORE_MODULES_H\n"
    "#define SHUTTLESOCK_CORE_MODULES_H\n"
    "#include <shuttlesock/common.h>\n"
    "${core_modules_includes}\n"
    "#endif //SHUTTLESOCK_CORE_MODULES_H\n"
  )
endfunction()
