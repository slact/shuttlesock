set(LUA_RELEASE_VERSION "5.4.0")
set(LUA_RELEASE_MD5 dbf155764e5d433fc55ae80ea7060b60 )

set(SHUTTLESOCK_LUA_MIN_VERSION "5.4" CACHE INTERNAL "Lua version minimum requirement")
set(LUA_MIN_VERSION "5.4" )
string(REPLACE "\." "" LUA_MIN_VERSION_NO_DOT "${LUA_MIN_VERSION}")

function(shuttlesock_link_lua STATIC_BUILD LUA_EXTRA_CFLAGS)
  
  if(NOT STATIC_BUILD)
    set(Lua_FIND_VERSION "${LUA_MIN_VERSION}")
    include(FindLua)
    if(LUA_INCLUDE_DIR)
      target_include_directories(shuttlesock PUBLIC ${LUA_INCLUDE_DIR})
      target_link_libraries(shuttlesock PUBLIC ${LUA_LIBRARIES})
    else()
      message(STATUS "Could not find Lua ${Lua_FIND_VERSION}. Will build from source.")
      set(STATIC_BUILD ON)
    endif()
  endif()
  if(STATIC_BUILD)
    #we want to see if Lua is installed locally to copy its package.path and package.cpath
    find_program(LUA_BINARY NAMES "lua${LUA_MIN_VERSION_NO_DOT}" "lua${LUA_MIN_VERSION}" lua)
    if(LUA_BINARY)
      execute_process(
        COMMAND "${LUA_BINARY}" -v
        OUTPUT_VARIABLE lua_output
      )
      message("version output: ${lua_output}")
      string(FIND "${lua_output}" "Lua ${LUA_MIN_VERSION}" lua_version_match)
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
    
    shuttlesock_build_lua("${LUA_EXTRA_CFLAGS}")
    target_link_libraries(shuttlesock PUBLIC ${THIRDPARTY_PREFIX}/lib/liblua.a)
    if(CMAKE_SYSTEM_NAME STREQUAL Linux)
      target_link_libraries(shuttlesock PRIVATE m) #lua links to libm on linux only
    endif()
    target_link_libraries(shuttlesock PRIVATE dl)
  endif()
endfunction()

function (shuttlesock_build_lua LUA_EXTRA_CFLAGS)
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
  
  if(CMAKE_OSX_SYSROOT)
    set(LUA_ISYSROOT "-isysroot ${CMAKE_OSX_SYSROOT}")
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
      "MYCFLAGS=${SHUTTLESOCK_SHARED_CFLAGS} -O${OPTIMIZE_LEVEL} ${LUA_EXTRA_CFLAGS} ${LUA_ISYSROOT} -fPIC -g -DLUA_COMPAT_5_3 -DLUA_COMPAT_5_2 -DLUA_COMPAT_5_1"
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
