project                     (DIY)
cmake_minimum_required      (VERSION 2.8)

option                      (debug              "Build DIY with debugging on"                OFF)
option                      (optimize           "Build DIY with optimization"                ON)
option                      (threads            "Build DIY with threading"                   ON)

find_package                (MPI REQUIRED)
set                         (mpi_libraries              ${MPI_C_LIBRARIES} ${MPI_CXX_LIBRARIES})
set                         (libraries ${libraries}     ${mpi_libraries})

# Debugging
if                          (debug)
    if                      (optimize)
            set             (cxx_flags                  ${CMAKE_CXX_FLAGS_RELWITHDEBINFO})
    else                    (optimize)
            set             (cxx_flags                  ${CMAKE_CXX_FLAGS_DEBUG})
    endif                   (optimize)
    add_definitions         (-DDEBUG)
else                        (debug)
    if                      (optimize)
            set             (cxx_flags                  ${CMAKE_CXX_FLAGS_RELEASE})
    else                    (optimize)
            set             (cxx_flags                  ${CMAKE_CXX_FLAGS})
    endif                   (optimize)
endif                       (debug)
add_definitions             (${cxx_flags})

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
                             ${CMAKE_CURRENT_SOURCE_DIR}/include
                             SYSTEM ${MPI_INCLUDE_PATH})

enable_testing              ()
add_subdirectory            (examples)
add_subdirectory            (tests)
