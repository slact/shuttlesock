include(TargetRequirePackage)

function(shuttlesock_link_libev STATIC_BUILD)
  set(LIBEV_VERSION "4.31")
  
  if(NOT STATIC_BUILD)
    target_require_package(shuttlesock PUBLIC ev)
  else()
    
    include(ProcessorCount)
    ProcessorCount(processor_count)
    if(NOT processor_count EQUAL 0)
      set(LIBEV_MAKE_PARALLEL_FLAG -j${processor_count})
    endif()
    
    set(LIBEV_PREFIX_DIR ${CMAKE_CURRENT_BINARY_DIR}/libev)
    include(ExternalProject)
    ExternalProject_Add(libev_autoconf
      URL "http://dist.schmorp.de/libev/Attic/libev-${LIBEV_VERSION}.tar.gz"
    #  SOURCE_DIR ${LIBEV_DIR}
      CONFIGURE_COMMAND /bin/sh -c "CFLAGS=\"${SHUTTLESOCK_SHARED_CFLAGS} -O${OPTIMIZE_LEVEL} -w\" LDFLAGS=\"${SHUTTLESOCK_SHARED_LDFLAGS}\" CC=\"${SHUTTLESOCK_SHARED_CC}\" ./configure --prefix=\"${LIBEV_PREFIX_DIR}\" --enable-shared=no --with-pic=yes"
      PREFIX ${LIBEV_PREFIX_DIR}
      BUILD_COMMAND make ${LIBEV_MAKE_PARALLEL_FLAG}
      INSTALL_COMMAND make install
      BUILD_BYPRODUCTS ${LIBEV_PREFIX_DIR}/lib/libev.a
      BUILD_IN_SOURCE 1
    )
    target_include_directories(shuttlesock PUBLIC ${LIBEV_PREFIX_DIR}/include)
    add_dependencies(shuttlesock libev_autoconf)
    add_library(libev STATIC IMPORTED)
    set_target_properties(libev PROPERTIES IMPORTED_LOCATION ${LIBEV_PREFIX_DIR}/lib/libev.a)
    target_link_libraries(shuttlesock PRIVATE libev)
  endif()
endfunction()
