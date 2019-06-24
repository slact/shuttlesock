function(target_require_package target scope name)
  set(oneValueArgs HEADER_NAME OPTIONAL)
  cmake_parse_arguments(REQUIRE_PACKAGE "" "${oneValueArgs}" "" "${ARGN}")
  
  include(GNUInstallDirs)
  string(TOUPPER ${name} NAME)
  set(libname lib${name})
  string(TOUPPER ${libname} LIBNAME)
  string(TOUPPER ${scope} scope)
  
  
  set(possible_scopes PUBLIC PRIVATE)
  if(NOT scope IN_LIST possible_scopes)
    message(FATAL_ERROR "target_require_package scope (2nd argument) must be PRIVATE or PUBLIC, was ${scope}")
  endif()
  find_package(${name} MODULE QUIET)
  
  if(${LIBNAME}_FOUND)
    #message("found ${name} with find_package()")
  else()
    if(NOT DEFINED REQUIRE_PACKAGE_HEADER_NAME)
      set(REQUIRE_PACKAGE_HEADER_NAME "${name}.h")
    endif()
    find_path("${LIBNAME}_INCLUDE_DIR"
      NAMES
        ${REQUIRE_PACKAGE_HEADER_NAME}
      HINTS
        "${PC_${LIBNAME}_INCLUDEDIR}"
        "${PC_${LIBNAME}_INCLUDE_DIRS}"
    )

    find_library(${LIBNAME}_LIBRARY
      NAMES
        "${name}"
      HINTS 
        ${PC_${LIBNAME}_LIBDIR} ${PC_${LIBNAME}_LIBRARY_DIRS}
    )
  endif()
  
  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(${libname} 
    FOUND_VAR
      "${LIBNAME}_FOUND"
    REQUIRED_VARS
      "${LIBNAME}_LIBRARY" "${LIBNAME}_INCLUDE_DIR"
  )
  mark_as_advanced("${LIBNAME}_INCLUDE_DIR" "${LIBNAME}_LIBRARY" "${LIBNAME}_FOUND")
  if(${LIBNAME}_FOUND)
    target_include_directories(${target} ${scope} "${${LIBNAME}_INCLUDE_DIR}")
    target_link_libraries(${target} ${scope} "${${LIBNAME}_LIBRARY}")
  elseif(NOT REQUIRE_PACKAGE_OPTIONAL)
    message(SEND_ERROR "Failed to find library ${name}")
  endif()
  if(REQUIRE_PACKAGE_OPTIONAL)
    set(${REQUIRE_PACKAGE_OPTIONAL} ${${LIBNAME}_FOUND} PARENT_SCOPE)
  endif()
  
endfunction()
