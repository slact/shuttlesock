if(CMAKE_BUILD_TYPE MATCHES "GCC")
  set(CMAKE_C_COMPILER gcc)
  set(CMAKE_CXX_COMPILER g++)
elseif(CMAKE_BUILD_TYPE MATCHES "Clang")
  set(CMAKE_C_COMPILER clang)
  set(CMAKE_CXX_COMPILER clang++)
endif()
