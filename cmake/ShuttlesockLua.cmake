function(shuttlesock_link_lua STATIC_BUILD LUA_EXTRA_CFLAGS)
  #lua (we want 5.3)
  if(NOT STATIC_BUILD)
    set(Lua_FIND_VERSION 5.3)
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
    
    
    set(LUA_PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/src/lua_static/)
    include(ExternalProject)
    
    include(ProcessorCount)
    ProcessorCount(processor_count)
    if(NOT processor_count EQUAL 0)
      set(LUA_MAKE_PARALLEL_FLAG -j${processor_count})
    endif()
    
    ExternalProject_Add(lua_static
      URL "https://www.lua.org/ftp/lua-5.3.5.tar.gz"
      CONFIGURE_COMMAND ""
      PREFIX ${CMAKE_CURRENT_BINARY_DIR}
      BUILD_COMMAND make "CC=${SHUTTLESOCK_SHARED_CC}" "MYCFLAGS=${SHUTTLESOCK_SHARED_CFLAGS} -O${OPTIMIZE_LEVEL} ${LUA_EXTRA_CFLAGS} -fPIC -g -DLUA_COMPAT_5_2 -DLUA_COMPAT_5_1" "MYLDFLAGS=${SHUTTLESOCK_SHARED_LDFLAGS}" ${LUA_MAKE_PARALLEL_FLAG} ${LUA_BUILD_TARGET}
      INSTALL_COMMAND ""
      BUILD_BYPRODUCTS ${LUA_PREFIX_DIR}/src/liblua.a
      BUILD_IN_SOURCE 1
    )
    target_include_directories(shuttlesock PUBLIC ${LUA_PREFIX_DIR}/src)
    add_dependencies(shuttlesock lua_static)
    add_library(lualib STATIC IMPORTED)
    set_target_properties(lualib PROPERTIES IMPORTED_LOCATION ${LUA_PREFIX_DIR}/src/liblua.a)
    target_link_libraries(shuttlesock PRIVATE lualib)
    target_link_libraries(shuttlesock PRIVATE dl)
  endif()
endfunction()