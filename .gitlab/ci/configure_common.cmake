set(CTEST_USE_LAUNCHERS "ON" CACHE STRING "")

set(build_examples ON CACHE BOOL "")
set(build_tests ON CACHE BOOL "")
set(log ON CACHE BOOL "")
set(mpi ON CACHE BOOL "")
set(threads ON CACHE BOOL "")

include("${CMAKE_CURRENT_LIST_DIR}/configure_sccache.cmake")
