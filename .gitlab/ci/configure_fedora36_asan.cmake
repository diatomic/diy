set(enable_sanitizers ON CACHE BOOL "")
set(sanitizer "address" CACHE STRING "")

include("${CMAKE_CURRENT_LIST_DIR}/configure_fedora36_mpich.cmake")
