set(enable_sanitizers ON CACHE BOOL "")
set(sanitizer "address" CACHE STRING "")

include("${CMAKE_CURRENT_LIST_DIR}/configure_fedora42_mpich.cmake")
