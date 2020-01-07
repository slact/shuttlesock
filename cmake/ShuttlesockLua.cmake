set(LUA_RELEASE_VERSION "5.3.5")
set(LUA_RELEASE_MD5 4f4b4f323fd3514a68e0ab3da8ce3455)

set(LUA_MIN_VERSION "5.3")

function(shuttlesock_link_lua STATIC_BUILD LUA_EXTRA_CFLAGS)
  #lua (we want 5.3)
  
  if(NOT STATIC_BUILD)
    set(Lua_FIND_VERSION "${LUA_MIN_VERSION}")
    include(FindLua)
    if(LUA_INCLUDE_DIR)
      target_include_directories(shuttlesock PUBLIC ${LUA_INCLUDE_DIR})
      target_link_libraries(shuttlesock PUBLIC ${LUA_LIBRARIES})
    else()
      message(FATAL_ERROR "Failed to find Lua ${Lua_FIND_VERSION}")
    endif()
  else()
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
    
    #we want to see if Lua is installed locally to copy its package.path and package.cpath
    find_program(LUA_BINARY NAMES lua53 lua5.3 lua)
    if(LUA_BINARY)
      execute_process(
        COMMAND "${LUA_BINARY}" -v
        OUTPUT_VARIABLE lua_output
      )
      message("version output: ${lua_output}")
      string(FIND "${lua_output}" "Lua 5.3" lua_version_match)
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
      DOWNLOAD_DIR ${CMAKE_CURRENT_LIST_DIR}/.cmake_downloads
      CONFIGURE_COMMAND ""
      PREFIX ${LUA_PREFIX_DIR}
      BUILD_COMMAND make 
        "CC=${SHUTTLESOCK_SHARED_CC}"
        "MYCFLAGS=${SHUTTLESOCK_SHARED_CFLAGS} -O${OPTIMIZE_LEVEL} ${LUA_EXTRA_CFLAGS} -fPIC -g -DLUA_COMPAT_5_2 -DLUA_COMPAT_5_1"
        "MYLDFLAGS=${SHUTTLESOCK_SHARED_LDFLAGS}"
        ${LUA_MAKE_PARALLEL_FLAG}
        ${LUA_BUILD_TARGET}
      INSTALL_COMMAND make "INSTALL_TOP=${LUA_PREFIX_DIR}" install
      BUILD_BYPRODUCTS ${LUA_PREFIX_DIR}/lib/liblua.a
      BUILD_IN_SOURCE 1
    )
    
    ExternalProject_Add_Step(lua symlink_includes
      COMMAND ${CMAKE_COMMAND} -E create_symlink  "${LUA_PREFIX_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/src/include/shuttlesock/lua"
    )
    target_include_directories(shuttlesock PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/src/include/shuttlesock/lua")
    
    target_link_libraries(shuttlesock PUBLIC ${LUA_PREFIX_DIR}/lib/liblua.a)
    target_link_libraries(shuttlesock PRIVATE dl)
    
    add_dependencies(shuttlesock lua)
  endif()
endfunction()
