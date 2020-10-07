project                     (pyDIY)
cmake_minimum_required      (VERSION 3.0)

add_subdirectory            (ext/pybind11)

# Copy diy/*.py
file                        (MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/diy")
file                        (GLOB PYDIY_PYTHON "${CMAKE_CURRENT_SOURCE_DIR}/diy/*.py")
add_custom_target           (pydiy ALL ${CMAKE_COMMAND} -E copy_directory
                                   ${CMAKE_CURRENT_SOURCE_DIR}/diy ${CMAKE_CURRENT_BINARY_DIR}/diy
                                   DEPENDS ${PYDIY_PYTHON})

# Build diy/_diy.so
set                         (DIY_SOURCES
                                src/assigner.cpp
                                src/decomposer.cpp
                                src/diy.cpp
                                src/io.cpp
                                src/link.cpp
                                src/master.cpp
                                src/mpi.cpp
                                src/myblock.cpp
                                src/partners.cpp
                                src/proxy.cpp
                                src/reducer.cpp
                                src/serialization.cpp
                                src/types.cpp)

pybind11_add_module         (_diy ${DIY_SOURCES})
set_target_properties       (_diy PROPERTIES OUTPUT_NAME diy/_diy)

## MPI
target_link_libraries       (_diy PRIVATE ${libraries})

add_subdirectory            (examples)
