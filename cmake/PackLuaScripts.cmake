include(CMakeParseArguments)

function(pack_lua_scripts_start)
  set(oneValueArgs SOURCE)
  set(SHUTTLESOCK_LUA_SCRIPT_NAMES "" CACHE INTERNAL "lua script names" FORCE)
  set(SHUTTLESOCK_LUA_SCRIPT_SCRIPTS "" CACHE INTERNAL "lua scripts" FORCE)
  set(SHUTTLESOCK_LUA_SCRIPT_IS_MODULE "" CACHE INTERNAL "lua script as a module" FORCE)
  cmake_parse_arguments(LUA_SCRIPTS "" "${oneValueArgs}" "" ${ARGN})
  set(SHUTTLESOCK_LUA_SCRIPTS_SOURCE "${LUA_SCRIPTS_SOURCE}" CACHE INTERNAL "lua scripts source file" FORCE)
endfunction()

function(pack_lua_scripts_add)
  set(options MODULE)
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
endfunction()

function(pack_lua_scripts_finish)
  list(LENGTH SHUTTLESOCK_LUA_SCRIPT_NAMES SCRIPT_COUNT)
  foreach(SCRIPT_NAME IN ITEMS ${SHUTTLESOCK_LUA_SCRIPT_NAMES})
    list(FIND SHUTTLESOCK_LUA_SCRIPT_NAMES "${SCRIPT_NAME}" SCRIPT_N)
    list(GET SHUTTLESOCK_LUA_SCRIPT_FILES ${SCRIPT_N} SCRIPT_FILE)
    
    list(GET SHUTTLESOCK_LUA_SCRIPT_FILES ${SCRIPT_N} SCRIPT_FILE)
    list(GET SHUTTLESOCK_LUA_SCRIPT_IS_MODULE ${SCRIPT_N} SCRIPT_IS_MODULE)
    
    # reads source file contents as text string
    file(READ ${SCRIPT_FILE} SCRIPT_STRING)
    string(LENGTH "${SCRIPT_STRING}" SCRIPT_STRING_LENGTH)
    
    #escape quotes and newlines, and indent the newlines
    string(REGEX REPLACE "\\\\" "\\\\\\\\" SCRIPT_STRING "${SCRIPT_STRING}")
    string(CONFIGURE "\${SCRIPT_STRING}" SCRIPT_STRING ESCAPE_QUOTES)
    string(REGEX REPLACE "\n" "\\\\n\"\n      \"" SCRIPT_STRING "${SCRIPT_STRING}")
    
    string(APPEND SCRIPTS_STRUCT
      "  {\n"
      "    .name = \"${SCRIPT_NAME}\",\n"
      "    .module = ${SCRIPT_IS_MODULE},\n"
      "    .script = \"${SCRIPT_STRING}\",\n"
      "    .strlen = ${SCRIPT_STRING_LENGTH}\n"
      "  },\n"
    )
  endforeach()
  
  file(WRITE "${PROJECT_BINARY_DIR}/src/${SHUTTLESOCK_LUA_SCRIPTS_SOURCE}"
    "//auto-generated original content, pls don't steal\n"
    "#include <shuttlesock/embedded_lua_scripts.h>\n"
    "shuso_lua_embedded_scripts_t shuttlesock_lua_embedded_scripts[] = {\n"
    "${SCRIPTS_STRUCT}\n"
    "  {NULL,false,NULL,0}\n"
    "};"
  )
  
endfunction()
