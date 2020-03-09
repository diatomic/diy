set(enable_sanitizers ON CACHE BOOL "")
set(sanitizer "undefined" CACHE STRING "")

include("${CMAKE_CURRENT_LIST_DIR}/configure_fedora34_mpich.cmake")
