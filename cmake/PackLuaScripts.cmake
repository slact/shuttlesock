include(CMakeParseArguments)

function(pack_lua_scripts_start)
  set(oneValueArgs SOURCE SCRIPTS_PATH)
  set(SHUTTLESOCK_LUA_SCRIPT_NAMES "" CACHE INTERNAL "lua script names" FORCE)
  set(SHUTTLESOCK_LUA_SCRIPT_SCRIPTS "" CACHE INTERNAL "lua scripts" FORCE)
  set(SHUTTLESOCK_LUA_SCRIPT_IS_MODULE "" CACHE INTERNAL "lua script as a module" FORCE)
  cmake_parse_arguments(LUA_SCRIPTS "" "${oneValueArgs}" "" ${ARGN})
  set(SHUTTLESOCK_LUA_SCRIPTS_SOURCE "${LUA_SCRIPTS_SOURCE}" CACHE INTERNAL "lua scripts source file" FORCE)
  set(SHUTTLESOCK_LUA_SCRIPTS_PATH "${LUA_SCRIPTS_SCRIPTS_PATH}" CACHE INTERNAL "lua scripts path" FORCE)
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
  
  find_program(LUACHECK_PROGRAM NAMES luacheck)
  
endfunction()

function(pack_lua_scripts_add)
  set(options MODULE CHECK)
  set(oneValueArgs NAME SCRIPT)
  cmake_parse_arguments(LUA_SCRIPTS "${options}" "${oneValueArgs}" "" ${ARGN})
  list(APPEND SHUTTLESOCK_LUA_SCRIPT_NAMES ${LUA_SCRIPTS_NAME})
  list(APPEND SHUTTLESOCK_LUA_SCRIPT_FILES ${LUA_SCRIPTS_SCRIPT})
  if("${LUA_SCRIPTS_MODULE}")
    list(APPEND SHUTTLESOCK_LUA_SCRIPT_IS_MODULE "true")
  else()
    list(APPEND SHUTTLESOCK_LUA_SCRIPT_IS_MODULE "false")
  endif()
  set(SHUTTLESOCK_LUA_SCRIPT_NAMES "${SHUTTLESOCK_LUA_SCRIPT_NAMES}" CACHE INTERNAL "lua script names" FORCE)
  set(SHUTTLESOCK_LUA_SCRIPT_FILES "${SHUTTLESOCK_LUA_SCRIPT_FILES}" CACHE INTERNAL "lua script files" FORCE)
  set(SHUTTLESOCK_LUA_SCRIPT_IS_MODULE "${SHUTTLESOCK_LUA_SCRIPT_IS_MODULE}" CACHE INTERNAL "lua script as a module" FORCE)
  if(LUA_SCRIPTS_CHECK)
    message(STATUS "Check Lua script ${LUA_SCRIPTS_SCRIPT}")
    if(LUACHECK_PROGRAM)
      execute_process(
        COMMAND "${LUACHECK_PROGRAM}" "${LUA_SCRIPTS_SCRIPT}"
        RESULT_VARIABLE luacheck_res
        OUTPUT_VARIABLE luacheck_out
        ERROR_VARIABLE luacheck_err
        WORKING_DIRECTORY "${SHUTTLESOCK_LUA_SCRIPTS_PATH}"
      )
      if(NOT("${luacheck_res}" EQUAL 0))
        message(FATAL_ERROR "Check Lua script ${LUA_SCRIPTS_SCRIPT} - failed:\n ${luacheck_out} ${luacheck_err}")
      endif()
      message(STATUS "Check Lua script ${LUA_SCRIPTS_SCRIPT} - ok")
    else()
      message(STATUS "Check Lua script ${LUA_SCRIPTS_SCRIPT} - skipped (no luacheck found)")
    endif()
  endif()
endfunction()

function(pack_lua_scripts_finish)
  list(LENGTH SHUTTLESOCK_LUA_SCRIPT_NAMES SCRIPT_COUNT)
  
  set(SHUTTLESOCK_LUA_SCRIPTS_COMPILED "${LUAC_PROGRAM}" CACHE INTERNAL "compiled lua scripts" FORCE)
  
  foreach(SCRIPT_NAME IN ITEMS ${SHUTTLESOCK_LUA_SCRIPT_NAMES})
    list(FIND SHUTTLESOCK_LUA_SCRIPT_NAMES "${SCRIPT_NAME}" SCRIPT_N)
    list(GET SHUTTLESOCK_LUA_SCRIPT_FILES ${SCRIPT_N} SCRIPT_FILE)
    
    list(GET SHUTTLESOCK_LUA_SCRIPT_FILES ${SCRIPT_N} SCRIPT_FILE)
    list(GET SHUTTLESOCK_LUA_SCRIPT_IS_MODULE ${SCRIPT_N} SCRIPT_IS_MODULE)
    
    if(LUAC_PROGRAM)
      set(luac_outfile ${PROJECT_BINARY_DIR}/pack_lua_scripts/${SCRIPT_FILE}.luac.out)
      file(WRITE "${luac_outfile}" "")
      message(STATUS "Compiling Lua script ${SCRIPT_FILE}")
      execute_process(
        COMMAND "${LUAC_PROGRAM}" -o "${luac_outfile}" ${SCRIPT_FILE}
        WORKING_DIRECTORY "${SHUTTLESOCK_LUA_SCRIPTS_PATH}"
        RESULT_VARIABLE luac_res
        OUTPUT_VARIABLE luac_out
        ERROR_VARIABLE luac_err
      )
      if(NOT("${luac_res}" EQUAL 0))
        message(FATAL_ERROR "Failed to compile Lua script ${SCRIPT_FILE}: ${luac_err}")
      endif()
      
      file(READ "${luac_outfile}" SCRIPT_STRING HEX)
      
      string(LENGTH "${SCRIPT_STRING}" SCRIPT_STRING_LENGTH)
      math(EXPR SCRIPT_STRING_LENGTH "${SCRIPT_STRING_LENGTH} / 2")
      
      string(REGEX REPLACE "([0-9a-f][0-9a-f])" "\\\\x\\1" SCRIPT_STRING ${SCRIPT_STRING})
      set(script_key "compiled")
      set(script_len_key "compiled_len")
      message(STATUS "Compiling Lua script ${SCRIPT_FILE} - ok")
    else()
      message(STATUS "Including Lua script ${SCRIPT_FILE}")
      # reads source file contents as text string
      file(READ "${SHUTTLESOCK_LUA_SCRIPTS_PATH}/${SCRIPT_FILE}" SCRIPT_STRING)
      string(LENGTH "${SCRIPT_STRING}" SCRIPT_STRING_LENGTH)
      
      #escape quotes and newlines, and indent the newlines
      string(REGEX REPLACE "\\\\" "\\\\\\\\" SCRIPT_STRING "${SCRIPT_STRING}")
      string(CONFIGURE "\${SCRIPT_STRING}" SCRIPT_STRING ESCAPE_QUOTES)
      string(REGEX REPLACE "\n" "\\\\n\"\n      \"" SCRIPT_STRING "${SCRIPT_STRING}")
      
      set(script_key "script")
      set(script_len_key "script_len")
      message(STATUS "Including Lua script ${SCRIPT_FILE} - ok")
    endif()
    
    string(APPEND SCRIPTS_STRUCT
      "  {\n"
      "    .name = \"${SCRIPT_NAME}\",\n"
      "    .module = ${SCRIPT_IS_MODULE},\n"
      "    .${script_key} = \"${SCRIPT_STRING}\",\n"
      "    .${script_len_key} = ${SCRIPT_STRING_LENGTH}\n"
      "  },\n"
    )
  endforeach()
  
  file(REMOVE_RECURSE "${PROJECT_BINARY_DIR}/pack_lua_scripts/")
  
  file(WRITE "${PROJECT_BINARY_DIR}/src/${SHUTTLESOCK_LUA_SCRIPTS_SOURCE}"
    "//auto-generated original content, pls don't steal\n"
    "#include <shuttlesock/embedded_lua_scripts.h>\n"
    "shuso_lua_embedded_scripts_t shuttlesock_lua_embedded_scripts[] = {\n"
    "${SCRIPTS_STRUCT}\n"
    "  {.name=NULL,.script=NULL}\n"
    "};\n"
  )
  
endfunction()
