project                     (DIY)
cmake_minimum_required      (VERSION 3.0)

option                      (threads            "Build DIY with threading"                   ON)
option                      (log                "Build DIY with logging"                     OFF)
option                      (profile            "Build DIY with profiling"                   OFF)
option                      (mpi                "Build DIY with mpi"                         ON)

# Default to Release
if                          (NOT CMAKE_BUILD_TYPE)
    set                     (CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
    set_property            (CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif                       (NOT CMAKE_BUILD_TYPE)

# Debugging
if                          (${CMAKE_BUILD_TYPE} STREQUAL "Debug" OR
                             ${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
    add_definitions         (-DDEBUG)
endif                       ()

# Logging
if                          (log)
    add_definitions         (-DDIY_USE_SPDLOG)
    find_path               (SPDLOG_INCLUDE_DIR     spdlog/spdlog.h)
    include_directories     (${SPDLOG_INCLUDE_DIR})
endif()

# Profiling
if                          (profile)
    add_definitions         (-DDIY_PROFILE)
endif()

# C++11
set                         (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

if                          (NOT mpi)
    add_definitions         (-DDIY_NO_MPI)
else                        (NOT mpi)
    find_package                (MPI REQUIRED)
    set                         (mpi_libraries              ${MPI_C_LIBRARIES} ${MPI_CXX_LIBRARIES})
    set                         (libraries ${libraries}     ${mpi_libraries})
endif                       (NOT mpi)

if                          (NOT threads)
    add_definitions         (-DDIY_NO_THREADS)
else                        (NOT threads)
    find_package            (Threads)
    set                     (libraries ${libraries}     ${CMAKE_THREAD_LIBS_INIT})
endif                       (NOT threads)

# Set includes
set                         (CMAKE_INCLUDE_SYSTEM_FLAG_CXX "-isystem")
include_directories         (${CMAKE_CURRENT_BINARY_DIR}
                             ${CMAKE_CURRENT_SOURCE_DIR}
                             ${CMAKE_CURRENT_SOURCE_DIR}/include)
if                          (mpi)
    include_directories     (SYSTEM ${MPI_INCLUDE_PATH})
endif                       (mpi)

enable_testing              ()
add_subdirectory            (examples)
add_subdirectory            (tests)
