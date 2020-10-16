set(SHUTTLESOCK_LUA_DEFAULT_VERSION "5.4" CACHE INTERNAL "Lua default version")

set(LUA_54_RELEASE_VERSION "5.4.1")
set(LUA_54_RELEASE_MD5 1d575faef1c907292edd79e7a2784d30 )

set(LUA_53_RELEASE_VERSION "5.3.6")
set(LUA_53_RELEASE_MD5 83f23dbd5230140a3770d5f54076948d )

set(LUA_SUPPORTED_VERSIONS_INFO_STRING "5.3, 5.4")

function(shuttlesock_link_lua LUA_VERSION STATIC_BUILD LUA_EXTRA_CFLAGS)
  
  if(LUA_VERSION STREQUAL "")
    set(LUA_VERSION "${SHUTTLESOCK_LUA_DEFAULT_VERSION}")
  endif()
  
  if(LUA_VERSION MATCHES "^[0-9]+\.[0-9]+\.[0-9]+$")
    message(FATAL_ERROR "Lua Version too specific. Major and minor only (5.4), no patch (5.4.1)")
  elseif(LUA_VERSION MATCHES "^[0-9]+\.[0-9]+$")
    string(REPLACE "\." "" LUA_VERSION_NO_DOT "${LUA_VERSION}")
    set(LUA_RELEASE_VERSION "${LUA_${LUA_VERSION_NO_DOT}_RELEASE_VERSION}")
    set(LUA_RELEASE_MD5 "${LUA_${LUA_VERSION_NO_DOT}_RELEASE_MD5}")
    if(LUA_RELEASE_VERSION STREQUAL "")
      message(FATAL_ERROR "Lua version ${LUA_VERSION} not supported. (Supported: ${LUA_SUPPORTED_VERSIONS_INFO_STRING})")
    endif()
  else()
    message(FATAL_ERROR "Invalid Lua version ${LUA_VERSION}.  (Supported: ${LUA_SUPPORTED_VERSIONS_INFO_STRING})")
  endif()
  
  string(REPLACE "\." ";" LUA_VERSION_SPLIT "${LUA_VERSION}")
  
  if(NOT STATIC_BUILD)
    # the FindLua CMake module is a colossal atrocity,
    # just like all of CMake. Undocumented parameters, arcane variables,
    # fuck it into the deepest pits of hell.
    list(GET LUA_VERSION_SPLIT 0 Lua_FIND_VERSION_MAJOR)
    list(GET LUA_VERSION_SPLIT 1 Lua_FIND_VERSION_MINOR)
    set(Lua_FIND_VERSION_COUNT "${Lua_FIND_VERSION_MAJOR}")
    set(Lua_FIND_VERSION_EXACT "${LUA_VERSION}")
    message(STATUS "Check if Lua ${LUA_VERSION} is installed")
    include(FindLua)
    if(LUA_INCLUDE_DIR)
    message(STATUS "Check if Lua ${LUA_VERSION} is installed - yes (${LUA_VERSION_MAJOR}.${LUA_VERSION_MINOR}.${LUA_VERSION_PATCH})")
      target_include_directories(shuttlesock PUBLIC ${LUA_INCLUDE_DIR})
      target_link_libraries(shuttlesock PUBLIC ${LUA_LIBRARIES})
    else()
      message(STATUS "Check if Lua ${LUA_VERSION} is installed - no. Will build from source.")
      set(STATIC_BUILD ON)
    endif()
  endif()
  if(STATIC_BUILD)
    #we want to see if Lua is installed locally to copy its package.path and package.cpath
    find_program(LUA_BINARY NAMES "lua${LUA_VERSION_NO_DOT}" "lua${LUA_VERSION}" lua)
    if(LUA_BINARY)
      execute_process(
        COMMAND "${LUA_BINARY}" -v
        OUTPUT_VARIABLE lua_output
      )
      #message("version output: ${lua_output}")
      string(FIND "${lua_output}" "Lua ${LUA_VERSION}" lua_version_match)
      if(NOT "${lua_version_match}" EQUAL "-1")
        execute_process(
          COMMAND "${LUA_BINARY}" -e "io.stdout:write(package.path)"
          OUTPUT_VARIABLE lua_package_path
        )
        execute_process(
          COMMAND "${LUA_BINARY}" -e "io.stdout:write(package.cpath)"
          OUTPUT_VARIABLE lua_package_cpath
        )
        set(SHUTTLESOCK_LUA_PACKAGE_PATH "${lua_package_path}" CACHE INTERNAL "Lua package path")
        set(SHUTTLESOCK_LUA_PACKAGE_CPATH "${lua_package_cpath}" CACHE INTERNAL "Lua package cpath")
      endif()
    endif()
    
    shuttlesock_build_lua(${LUA_RELEASE_VERSION} ${LUA_RELEASE_MD5} "${LUA_EXTRA_CFLAGS}")
    target_link_libraries(shuttlesock PUBLIC ${THIRDPARTY_PREFIX}/lib/liblua.a)
    if(CMAKE_SYSTEM_NAME STREQUAL Linux)
      target_link_libraries(shuttlesock PRIVATE m) #lua links to libm on linux only
    endif()
    target_link_libraries(shuttlesock PRIVATE dl)
  endif()
endfunction()

function (shuttlesock_build_lua LUA_RELEASE_VERSION LUA_RELEASE_MD5 LUA_EXTRA_CFLAGS)
  if(SHUTTLESOCK_BUILD_LUA)
    #already building it
    return()
  else()
    set(SHUTTLESOCK_BUILD_LUA ON CACHE INTERNAL "")
  endif()
  
  if(CMAKE_SYSTEM_NAME STREQUAL Linux)
    set(LUA_BUILD_TARGET linux)
    target_link_libraries(shuttlesock PRIVATE m) #lua links to libm on linux only
  elseif(CMAKE_SYSTEM_NAME STREQUAL Darwin)
    set(LUA_BUILD_TARGET macosx)
  elseif(CMAKE_SYSTEM_NAME STREQUAL FreeBSD)
    set(LUA_BUILD_TARGET freebsd)
  elseif(CMAKE_SYSTEM_NAME STREQUAL Solaris)
    set(LUA_BUILD_TARGET solaris)
  elseif(CMAKE_SYSTEM_NAME STREQUAL BSD)
    set(LUA_BUILD_TARGET bsd)
  else()
    set(LUA_BUILD_TARGET posix)
  endif()
  
  set(LUA_PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/lua)
    
  include(ProcessorCount)
  ProcessorCount(processor_count)
  if(processor_count GREATER 1)
    set(LUA_MAKE_PARALLEL_FLAG -j${processor_count})
  endif()
  
  include(ExternalProject)
  ExternalProject_Add(lua
    URL "https://www.lua.org/ftp/lua-${LUA_RELEASE_VERSION}.tar.gz"
    URL_MD5 "${LUA_RELEASE_MD5}"
    DOWNLOAD_NO_PROGRESS 1
    DOWNLOAD_DIR "${THIRDPARTY_DOWNLOAD}"
    CONFIGURE_COMMAND ""
    PREFIX ${LUA_PREFIX_DIR}
    BUILD_COMMAND make 
      "CC=${SHUTTLESOCK_SHARED_CC}"
      "MYCFLAGS=${SHUTTLESOCK_SHARED_CFLAGS} -O${OPTIMIZE_LEVEL} ${LUA_EXTRA_CFLAGS} -fPIC -g -DLUA_COMPAT_5_3 -DLUA_COMPAT_5_2 -DLUA_COMPAT_5_1"
      "MYLDFLAGS=${SHUTTLESOCK_SHARED_LDFLAGS}"
      ${LUA_MAKE_PARALLEL_FLAG}
      ${LUA_BUILD_TARGET}
    INSTALL_COMMAND make "INSTALL_TOP=${THIRDPARTY_PREFIX}" install
    BUILD_BYPRODUCTS
      ${THIRDPARTY_PREFIX}/lib/liblua.a
      ${THIRDPARTY_PREFIX}/bin/lua
      ${THIRDPARTY_PREFIX}/bin/luac
      ${THIRDPARTY_PREFIX}/include/
    BUILD_IN_SOURCE 1
  )
  
  add_dependencies(shuttlesock lua)

endfunction()
