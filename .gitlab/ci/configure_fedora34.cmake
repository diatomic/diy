# Build binaries that will run on older architectures
if ("$ENV{CMAKE_CONFIGURATION}" MATCHES "fedora")
  set(CMAKE_C_FLAGS "-march=core2 -mno-avx512f" CACHE STRING "")
  set(CMAKE_CXX_FLAGS "-march=core2 -mno-avx512f" CACHE STRING "")
endif ()

include("${CMAKE_CURRENT_LIST_DIR}/configure_common.cmake")
