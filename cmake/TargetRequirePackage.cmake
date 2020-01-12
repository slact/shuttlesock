include(FindPackageHandleStandardArgs)
include(GNUInstallDirs)

function(target_require_package target scope name)
  set(oneValueArgs OPTIONAL INCLUDE_PATH_VAR LINK_LIB_VAR)
  cmake_parse_arguments(REQUIRE_PACKAGE "DRY_RUN;QUIET" "${oneValueArgs}" "HEADER_NAME;LIB_NAME;DISPLAY_NAME" "${ARGN}")
  string(TOUPPER ${name} NAME)
  set(libname lib${name})
  string(TOUPPER ${libname} LIBNAME)
  string(TOUPPER ${scope} scope)
  
  if(NOT REQUIRE_PACKAGE_DISPLAY_NAME)
    set(REQUIRE_PACKAGE_DISPLAY_NAME ${libname})
  endif()
  
  if(NOT REQUIRE_PACKAGE_HEADER_NAME)
    set(REQUIRE_PACKAGE_HEADER_NAME "${name}.h")
  endif()
  if(NOT REQUIRE_PACKAGE_LIB_NAME)
    set(REQUIRE_PACKAGE_LIB_NAME ${name})
  endif()
  
  if(NOT DEFINED ${LIBNAME}_FOUND)
    if(NOT REQUIRE_PACKAGE_QUIET)
      message(STATUS "Check if ${REQUIRE_PACKAGE_DISPLAY_NAME} is installed")
    endif()
    
    set(possible_scopes PUBLIC PRIVATE)
    if(NOT scope IN_LIST possible_scopes)
      message(FATAL_ERROR "target_require_package scope (2nd argument) must be PRIVATE or PUBLIC, was ${scope}")
    endif()
    find_package(${name} MODULE QUIET)
    if(${LIBNAME}_FOUND)
      #message("found ${name} with find_package()")
    else()
      find_path("${LIBNAME}_INCLUDE_DIR"
        NAMES
          ${REQUIRE_PACKAGE_HEADER_NAME}
        HINTS
          "${PC_${LIBNAME}_INCLUDEDIR}"
          "${PC_${LIBNAME}_INCLUDE_DIRS}"
      )

      find_library(${LIBNAME}_LIBRARY
        NAMES
          ${REQUIRE_PACKAGE_LIB_NAME}
        HINTS 
          ${PC_${LIBNAME}_LIBDIR} ${PC_${LIBNAME}_LIBRARY_DIRS}
      )
    endif()
    
    if(${LIBNAME}_LIBRARY AND ${LIBNAME}_INCLUDE_DIR)
      set(${LIBNAME}_FOUND YES)
    else()
      set(${LIBNAME}_FOUND "")
    endif()
    
    set(${LIBNAME}_INCLUDE_DIR "${${LIBNAME}_INCLUDE_DIR}" CACHE INTERNAL "")
    set(${LIBNAME}_LIBRARY "${${LIBNAME}_LIBRARY}" CACHE INTERNAL "")
    set(${LIBNAME}_FOUND "${${LIBNAME}_FOUND}" CACHE INTERNAL "")
    if(${LIBNAME}_FOUND AND NOT REQUIRE_PACKAGE_QUIET)
      message(STATUS "Check if ${REQUIRE_PACKAGE_DISPLAY_NAME} is installed - yes (${${LIBNAME}_LIBRARY})")
    elseif(NOT ${LIBNAME}_FOUND AND NOT REQUIRE_PACKAGE_QUIET)
        message(STATUS "Check if ${REQUIRE_PACKAGE_DISPLAY_NAME} is installed - no")
    endif()
  endif()
  
  if(${LIBNAME}_FOUND)
    if(NOT REQUIRE_PACKAGE_DRY_RUN)
      target_include_directories(${target} ${scope} "${${LIBNAME}_INCLUDE_DIR}")
      target_link_libraries(${target} ${scope} "${${LIBNAME}_LIBRARY}")
    endif()
    if(REQUIRE_PACKAGE_INCLUDE_PATH_VAR)
      set(${REQUIRE_PACKAGE_INCLUDE_PATH_VAR} "${${LIBNAME}_INCLUDE_DIR}" PARENT_SCOPE)
    endif()
    if(REQUIRE_PACKAGE_LINK_LIB_VAR)
      set(${REQUIRE_PACKAGE_LINK_LIB_VAR} "${${LIBNAME}_LIBRARY}" PARENT_SCOPE)
    endif()
    
  else()
    if(NOT REQUIRE_PACKAGE_OPTIONAL)
      message(SEND_ERROR "Failed to find library ${name}${fail_reason}")
    endif()
  endif()
  if(REQUIRE_PACKAGE_OPTIONAL)
    set(${REQUIRE_PACKAGE_OPTIONAL} ${${LIBNAME}_FOUND} PARENT_SCOPE)
  endif()
  
endfunction()
