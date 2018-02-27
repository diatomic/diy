include_directories         (${CMAKE_SOURCE_DIR}/examples)          # needed for opts.h
include_directories         (${CMAKE_SOURCE_DIR}/examples/reduce)   # needed for swap-reduce-test

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

add_executable              (mpi-collectives-test   mpi-collectives.cpp)
target_link_libraries       (mpi-collectives-test   ${libraries})

add_executable              (rexchange-test         rexchange.cpp)
target_link_libraries       (rexchange-test         ${libraries})

foreach                     (b 2 4 8 64)
    add_test                (NAME kd-tree-test-b${b}
                             COMMAND kd-tree-test -b ${b}
                            )
    add_test                (NAME kd-tree-test-sampling-b${b}
                             COMMAND kd-tree-test -s -b ${b}
                            )
    add_test                (NAME kd-tree-test-sampling-exponential-b${b}
                             COMMAND kd-tree-test -s -e -b ${b}
                            )
endforeach                  ()

add_test                    (NAME decomposer-test
                             COMMAND decomposer-test
                            )

if                          (mpi)
    # currently, I/O is only supported when built with MPI support.
    add_test                (NAME io-test
                             COMMAND io-test
                            )
endif                       (mpi)

foreach                     (b 2 4 8 9 12 24 36 44 48 56 64)
    add_test                (NAME swap-reduce-test-b${b}
                             COMMAND swap-reduce-test -b ${b}
                            )
    add_test                (NAME swap-reduce-test-k4-b${b}
                             COMMAND swap-reduce-test -k 4 -b ${b}
                            )
endforeach                  ()

if                          (mpi AND MPIEXEC)

  set                       (maxp 3)
  if                        (APPLE)
      set                   (maxp 2)
  endif()

  # mpi collectives test only makes sense when built with MPI support
  foreach                   (p RANGE 1 ${maxp})
      add_test              (NAME mpi-collectives-test-p${p}
                            COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:mpi-collectives-test>
                            )
  endforeach                (p)

  foreach                   (p RANGE 1 ${maxp})
      foreach               (b 2 4 8 9 12 24 36 44 48 56 64)
          add_test          (NAME merge-swap-reduce-test-p${p}-b${b}
                             COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:merge-swap-reduce-test> -b ${b}
                            )
      endforeach            (b)
  endforeach                (p)

  foreach                   (p 2 3)
      math                  (EXPR b "${p} + 1")
      add_test              (NAME simple-test-p${p}-b${b}
                             COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:simple-test> -b ${b}
                            )
  endforeach                (p)

  foreach                   (p RANGE 1 ${maxp})
      foreach               (b 2 4 8 9 12 24 36)
          add_test          (NAME rexchange-test-p${p}-b${b}
                             COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:rexchange-test> -b ${b}
                            )
          add_test          (NAME rexchange-test-p${p}-m1-b${b}
                             COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:rexchange-test> -m 1 -b ${b}
                            )
          add_test          (NAME rexchange-test-p${p}-t2-b${b}
                             COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:rexchange-test> -t 2 -b ${b}
                            )
      endforeach            (b)
  endforeach                (p)

else                        (mpi AND MPIEXEC)

  foreach                   (b 2 4 8 9 12 24 36 44 48 56 64)
      add_test              (NAME merge-swap-reduce-test-nompi-b${b}
                             COMMAND merge-swap-reduce-test -b ${b}
                            )
  endforeach                (b)

  foreach                   (b 3 4)
      add_test              (NAME simple-test-nompi-b${b}
                             COMMAND simple-test -b ${b}
                            )
  endforeach                (b)

  foreach                   (b 2 4 8 9 12 24 36)
      add_test              (NAME rexchange-test-nompi-b${b}
                             COMMAND rexchange-test -b ${b}
                            )
      add_test              (NAME rexchange-test-nompi-m1-b${b}
                             COMMAND rexchange-test -m 1 -b ${b}
                            )
      add_test              (NAME rexchange-test-nompi-t2-b${b}
                             COMMAND rexchange-test -t 2 -b ${b}
                            )
  endforeach                (b)

endif                       (mpi AND MPIEXEC)
