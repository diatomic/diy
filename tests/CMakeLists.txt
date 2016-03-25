include_directories         (${CMAKE_SOURCE_DIR}/examples)      # needed for opts.h

add_custom_target           (test-scripts ALL
                             ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/scripts" ${CMAKE_CURRENT_BINARY_DIR}/scripts
                            )

add_executable              (kd-tree-test           kd-tree.cpp)
target_link_libraries       (kd-tree-test           ${libraries})

add_test                    (kd-tree-test           scripts/kd-tree.sh)
add_test                    (kd-tree-test-sampling  scripts/kd-tree.sh -s)
