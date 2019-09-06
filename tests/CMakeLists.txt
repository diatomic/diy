include_directories         (${CMAKE_SOURCE_DIR}/examples)          # needed for opts.h
include_directories         (${CMAKE_SOURCE_DIR}/examples/reduce)   # needed for swap-reduce-test

add_executable              (kd-tree-test           kd-tree.cpp)
target_link_libraries       (kd-tree-test           ${libraries})

add_executable              (kd-tree-test2          kd-tree-dense-points.cpp)
target_link_libraries       (kd-tree-test2          ${libraries})

add_executable              (decomposer-test        decomposer.cpp)
target_link_libraries       (decomposer-test        ${libraries})

add_executable              (partners-test        partners.cpp)
target_link_libraries       (partners-test        ${libraries})

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

add_executable              (mpi-window-test        mpi-window.cpp)
target_link_libraries       (mpi-window-test        ${libraries})

add_executable              (dynamic-assigner-test  dynamic-assigner.cpp)
target_link_libraries       (dynamic-assigner-test  ${libraries})

add_executable              (rexchange-test         rexchange.cpp)
target_link_libraries       (rexchange-test         ${libraries})

add_executable              (iexchange-test         iexchange.cpp)
target_link_libraries       (iexchange-test         ${libraries})

add_executable              (grid-test              grid.cpp)
target_link_libraries       (grid-test              ${libraries})

add_executable              (serialization-test     serialization.cpp)
target_link_libraries       (serialization-test     ${libraries})

add_executable              (two-masters            two-masters.cpp)
target_link_libraries       (two-masters            ${libraries})

add_executable              (shared-output         shared-output.cpp)
target_link_libraries       (shared-output         ${libraries})

add_executable              (custom-bounds-test    custom-bounds.cpp)
target_link_libraries       (custom-bounds-test    ${libraries})

add_executable              (custom-bounds-load-test    custom-bounds-load.cpp)
target_link_libraries       (custom-bounds-load-test    ${libraries})

if                          (MSVC AND NOT(MSVC_VERSION LESS 1400))
    set_source_files_properties (kd-tree.cpp            PROPERTIES COMPILE_FLAGS "/bigobj")
    set_source_files_properties (merge-swap-reduce.cpp  PROPERTIES COMPILE_FLAGS "/bigobj")
    set_source_files_properties (swap-reduce.cpp        PROPERTIES COMPILE_FLAGS "/bigobj")
    set_source_files_properties (rexchange.cpp          PROPERTIES COMPILE_FLAGS "/bigobj")
endif                       ()


## Run the tests
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

foreach                     (b 2 4 8 16 32 64)
    foreach                 (n 128 256 512)
        add_test                (NAME kd-tree-test2-b${b}-n${n}
                                 COMMAND kd-tree-test2 -b ${b} -f ${CMAKE_CURRENT_SOURCE_DIR}/kd-tree-dataset.csv
                                )
    endforeach              ()
endforeach                  ()

add_test                    (NAME decomposer-test
                             COMMAND decomposer-test
                            )

add_test                    (NAME partners-test
                             COMMAND partners-test
                            )

add_test                    (NAME grid-test
                             COMMAND grid-test
                            )

add_test                    (NAME serialization-test
                             COMMAND serialization-test
                            )

if                          (mpi)
    # currently, I/O is only supported when built with MPI support.
    add_test                (NAME io-test
                             COMMAND io-test
                            )
    add_test                (NAME custom-bounds-test
                             COMMAND custom-bounds-test
                            )
    add_test                (NAME custom-bounds-load-test
                             COMMAND custom-bounds-load-test
                            )
    set_tests_properties    (custom-bounds-load-test PROPERTIES DEPENDS custom-bounds-test)
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
      add_test              (NAME mpi-window-test-p${p}
                            COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:mpi-window-test>
                            )
  endforeach                (p)

  foreach                   (p RANGE 1 ${maxp})
      add_test              (NAME dynamic-assigner-test-p${p}
                            COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:dynamic-assigner-test>
                            )
  endforeach                (p)

  foreach                   (p RANGE 1 ${maxp})
      foreach               (b 2 4 8 9 12 24 36 44 48 56 64)
          add_test          (NAME merge-swap-reduce-test-p${p}-b${b}
                             COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:merge-swap-reduce-test> -b ${b}
                            )
          add_test          (NAME merge-swap-reduce-test-p${p}-m1-b${b}
                             COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:merge-swap-reduce-test> -m 1 -b ${b}
                            )
          add_test          (NAME merge-swap-reduce-test-p${p}-t2-b${b}
                             COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:merge-swap-reduce-test> -t 2 -b ${b}
                            )
      endforeach            (b)
  endforeach                (p)

  foreach                   (p 2 3)
      math                  (EXPR b "${p} + 1")
      add_test              (NAME simple-test-p${p}-b${b}
                             COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:simple-test> -b ${b}
                            )
      set_tests_properties  (simple-test-p${p}-b${b} PROPERTIES TIMEOUT 300 RUN_SERIAL ON)
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

  foreach                   (p RANGE 1 ${maxp})
      foreach               (b 2 4 8 9 12 24 36)
          add_test          (NAME iexchange-test-p${p}-b${b}
                             COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:iexchange-test> -b ${b}
                            )
      endforeach            (b)
  endforeach                (p)

  foreach                   (p RANGE 1 ${maxp})
          add_test          (NAME two-masters-p${p}
                             COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:two-masters>
                            )
  endforeach                (p)

else                        (mpi AND MPIEXEC)

  add_test                  (NAME dynamic-assigner-test-p${p}
                            COMMAND dynamic-assigner-test
                            )

  add_test                  (NAME mpi-window-test-p${p}
                            COMMAND mpi-window-test
                            )

  foreach                   (b 2 4 8 9 12 24 36 44 48 56 64)
      add_test              (NAME merge-swap-reduce-test-nompi-b${b}
                             COMMAND merge-swap-reduce-test -b ${b}
                            )
      add_test              (NAME merge-swap-reduce-test-nompi-m1-b${b}
                             COMMAND merge-swap-reduce-test -m 1 -b ${b}
                            )
      add_test              (NAME merge-swap-reduce-test-nompi-t2-b${b}
                             COMMAND merge-swap-reduce-test -t 2 -b ${b}
                            )
  endforeach                (b)

  foreach                   (b 3 4)
      add_test              (NAME simple-test-nompi-b${b}
                             COMMAND simple-test -b ${b}
                            )
      set_tests_properties  (simple-test-nompi-b${b} PROPERTIES TIMEOUT 300 RUN_SERIAL ON)
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
