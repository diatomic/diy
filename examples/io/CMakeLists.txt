add_executable          (test-io    test-io.cpp)
target_link_libraries   (test-io    ${libraries})
file                    (COPY        ${CMAKE_CURRENT_SOURCE_DIR}/test.bin
                         DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file                    (COPY        ${CMAKE_CURRENT_SOURCE_DIR}/test.npy
                         DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
