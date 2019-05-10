function(target_require_package target scope name)
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
  if(${LIBNAME}_FOUND OR ${NAME}_FOUND)
    #message("found ${name} with find_package()")
  else()
    find_path("${LIBNAME}_INCLUDE_DIR"
      NAMES
        ${name}.h
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
  find_package_handle_standard_args(${libname} DEFAULT_MSG
                                    "${LIBNAME}_LIBRARY" "${LIBNAME}_INCLUDE_DIR")
  mark_as_advanced("${LIBNAME}_INCLUDE_DIR" "${LIBNAME}_LIBRARY")
  if(${LIBNAME}_FOUND OR ${NAME}_FOUND)
    target_include_directories(${target} ${scope} "${${LIBNAME}_INCLUDE_DIR}")
    target_link_libraries(${target} ${scope} "${${LIBNAME}_LIBRARY}")
  else()
    message(SEND_ERROR "Failed to find library ${name}")
  endif()
endfunction()
