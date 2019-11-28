include(CMakeParseArguments)

find_program(LUA_PROGRAM NAMES lua53 lua5.3 lua)
if(NOT LUA_PROGRAM)
  message(FATAL_ERROR "Could not find Lua program")
endif()
if(LUA_PROGRAM)
  execute_process(
    COMMAND "${LUA_PROGRAM}" -v
    OUTPUT_VARIABLE lua_output
  )
  string(FIND "${lua_output}" "Lua ${LUA_VERSION_STRING}" lua_version_match)
  if("${lua_version_match}" EQUAL "-1")
    message(FATAL_ERROR "Wrong Lua program version, expected ${LUA_VERSION_STRING}, got ${lua_output}")
  endif()
endif()

if(NOT DEFINED HAVE_LUACHECK)
  message(STATUS "Check if luacheck is installed ")
  execute_process(
    COMMAND "${LUA_PROGRAM}" "PackLuaScripts.lua" "--check"
    RESULT_VARIABLE luacheck_res
    OUTPUT_VARIABLE luacheck_out
    ERROR_VARIABLE luacheck_err
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
  )
  if("${luacheck_res}" EQUAL 0)
    message(STATUS "Check if luacheck is installed - yes")
    set(HAVE_LUACHECK "yes" CACHE INTERNAL "is the luacheck luarock installed?")
  else()
    message(STATUS "Check if luacheck is installed - no. Lua scripts will not be checked. ")
    set(HAVE_LUACHECK "no" CACHE INTERNAL "is the luacheck luarock installed?")
  endif()
endif()

if(NOT SHUTTLESOCK_NO_LUAC)
  find_program(LUAC_PROGRAM NAMES luac53 luac5.3 luac)
endif()
if(LUAC_PROGRAM)
  execute_process(
    COMMAND "${LUAC_PROGRAM}" -v
    OUTPUT_VARIABLE luac_output
  )
  string(FIND "${luac_output}" "Lua ${LUA_VERSION_STRING}" luac_version_match)
  if("${luac_version_match}" EQUAL "-1")
    set(LUAC_PROGRAM "" CACHE INTERNAL "luac path" FORCE)
  endif()
endif()

macro(pack_lua_script_internal module_name source_file)
  set(options MODULE CHECK BUNDLED)
  cmake_parse_arguments(LUA_SCRIPT "${options}" "" "" ${ARGN})
  set(outfile "${source_file}.out")
  if(LUA_SCRIPT_MODULE)
    set(script_kind "module")
    set(is_module "true")
  else()
    set(script_kind "script")
    set(is_module "false")
  endif()
  
  if(LUA_SCRIPT_BUNDLED)
    set(LUA_SCRIPT_CHECK "")
    set(is_bundled " bundled")
  endif()
  
  if("${LUA_SCRIPT_CHECK}" AND "${HAVE_LUACHECK}")
    set(checkarg "--check")
    set(command_check " and checking")
  else()
    set(checkarg "--nocheck")
    set(command_check "")
  endif()
  get_filename_component(outfile_dir ${outfile} DIRECTORY)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${outfile_dir})
  if(LUAC_PROGRAM)
    set(command_action "Compiling")
    set(compile_command ${LUAC_PROGRAM} -o ${CMAKE_CURRENT_BINARY_DIR}/${outfile} ${source_file})
    set(source_compiled "true")
  else()
    set(command_action "Including")
    set(compile_command ${CMAKE_COMMAND} -E copy "${source_file}" "${CMAKE_CURRENT_BINARY_DIR}/${outfile}")
    set(source_compiled "false")
  endif()
  add_custom_command(OUTPUT ${outfile}
    COMMAND ${LUA_PROGRAM} cmake/PackLuaScripts.lua ${checkarg} "${source_file}"
    COMMAND ${compile_command}
    DEPENDS ${source_file}
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    COMMENT "${command_action}${command_check}${is_bundled} Lua ${script_kind} \"${module_name}\" ${source_file}"
  )
  
  set(packed_lua_scripts_target_deps ${packed_lua_scripts_target_deps} ${outfile} CACHE INTERNAL "packed lua scripts deps" FORCE)
  set(packed_lua_scripts_config "${packed_lua_scripts_config} {src='${source_file}', name='${module_name}', file='${CMAKE_CURRENT_BINARY_DIR}/${outfile}', module=${is_module}, compiled=${source_compiled}}," CACHE INTERNAL "packed lua scripts config" FORCE)
endmacro()


function(pack_lua_module module_name source_file)
  pack_lua_script_internal(${module_name} ${source_file} MODULE CHECK)
endfunction()
function(pack_lua_bundled_module module_name source_file)
  pack_lua_script_internal(${module_name} ${source_file} MODULE BUNDLED)
endfunction()
function(pack_lua_script script_name source_file)
  pack_lua_script_internal(${script_name} ${source_file} CHECK)
endfunction()

function(set_lua_packed_script_file packed_filename)
  set(lua_packer_config_file "${CMAKE_CURRENT_BINARY_DIR}/src/lua_packer_config.lua")
  file(WRITE ${lua_packer_config_file}
    "return {${packed_lua_scripts_config}}"
  )

  add_custom_command(OUTPUT ${packed_filename}
    COMMAND ${LUA_PROGRAM} ${CMAKE_CURRENT_LIST_DIR}/cmake/PackLuaScripts.lua --pack ${lua_packer_config_file} ${CMAKE_CURRENT_BINARY_DIR}/${packed_filename}
    DEPENDS ${packed_lua_scripts_target_deps} ${lua_packer_config_file}
  )

endfunction()
