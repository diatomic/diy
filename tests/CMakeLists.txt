include_directories         (${CMAKE_SOURCE_DIR}/examples)          # needed for opts.h
include_directories         (${CMAKE_SOURCE_DIR}/examples/reduce)   # needed for swap-reduce-test

add_custom_target           (test-scripts ALL
                             ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/scripts" ${CMAKE_CURRENT_BINARY_DIR}/scripts
                            )

add_executable              (kd-tree-test           kd-tree.cpp)
target_link_libraries       (kd-tree-test           ${libraries})

add_executable              (decomposer-test        decomposer.cpp)
target_link_libraries       (decomposer-test        ${libraries})

add_executable              (swap-reduce-test       swap-reduce.cpp)
target_link_libraries       (swap-reduce-test       ${libraries})

add_executable              (merge-swap-reduce-test merge-swap-reduce.cpp)
target_link_libraries       (merge-swap-reduce-test ${libraries})

add_executable              (io-test                io.cpp)
target_link_libraries       (io-test                ${libraries})

add_executable              (simple-test            simple.cpp)
target_link_libraries       (simple-test            ${libraries})

add_test                    (kd-tree-test                       scripts/kd-tree.sh)
add_test                    (kd-tree-test-sampling              scripts/kd-tree.sh -s)
add_test                    (kd-tree-test-sampling-exponential  scripts/kd-tree.sh -s -e)

add_test                    (decomposer-test                    decomposer-test)

if                          (mpi)
    # currently, I/O is only supported when built with MPI support.
    add_test                (io-test                            io-test)
endif                       (mpi)

add_test                    (swap-reduce-test                   scripts/swap-reduce.sh)
add_test                    (swap-reduce-test-k4                scripts/swap-reduce.sh -k 4)

if                          (mpi)
    set                     (mpiexec_argument ${MPIEXEC})
else                        (mpi)
    set                     (mpiexec_argument)
endif                       (mpi)

add_test                    (merge-swap-reduce-test             scripts/merge-swap-reduce.sh ${mpiexec_argument})
add_test                    (simple-test                        scripts/simple.sh ${mpiexec_argument})
