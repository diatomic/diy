if                          (UNIX)
    add_subdirectory        (backward)
endif                       ()

include_directories         (${CMAKE_SOURCE_DIR}/examples)          # needed for opts.h
include_directories         (${CMAKE_SOURCE_DIR}/examples/reduce)   # needed for swap-reduce-test

function                    (compile_test target source)
    if                      (UNIX)
        add_executable          (${target} ${source} ${BACKWARD_ENABLE})
        target_link_libraries   (${target} ${libraries})
        add_backward            (${target})
    else                    ()
        add_executable          (${target} ${source})
        target_link_libraries   (${target} ${libraries})
    endif                   ()
endfunction                 ()

compile_test                (kd-tree-test           kd-tree.cpp)
compile_test                (kd-tree-test2          kd-tree-dense-points.cpp)
compile_test                (decomposer-test        decomposer.cpp)
compile_test                (partners-test          partners.cpp)
compile_test                (swap-reduce-test       swap-reduce.cpp)
compile_test                (merge-swap-reduce-test merge-swap-reduce.cpp)
compile_test                (io-test                io.cpp)
compile_test                (simple-test            simple.cpp)
compile_test                (blobs-test             blobs.cpp)
compile_test                (mpi-collectives-test   mpi-collectives.cpp)
compile_test                (mpi-window-test        mpi-window.cpp)
compile_test                (dynamic-assigner-test  dynamic-assigner.cpp)
compile_test                (rexchange-test         rexchange.cpp)
compile_test                (iexchange-test         iexchange.cpp)
compile_test                (grid-test              grid.cpp)
compile_test                (serialization-test     serialization.cpp)
compile_test                (two-masters            two-masters.cpp)
compile_test                (double-foreach         double-foreach.cpp)
compile_test                (shared-output          shared-output.cpp)
compile_test                (custom-bounds-test     custom-bounds.cpp)
compile_test                (custom-bounds-load-test    custom-bounds-load.cpp)
compile_test                (collective-balance-test    collective-balance.cpp)
compile_test                (sampling-balance-test      sampling-balance.cpp)

if (NOT build_diy_mpi_lib)
    compile_test            (mpi-type-conversions mpi-type-conversions.cpp)
    add_test                (NAME mpi-type-conversions COMMAND mpi-type-conversions)
endif()

if                          (MSVC AND NOT(MSVC_VERSION LESS 1400))
    set_source_files_properties (double-foreach.cpp     PROPERTIES COMPILE_FLAGS "/bigobj")
    set_source_files_properties (kd-tree.cpp            PROPERTIES COMPILE_FLAGS "/bigobj")
    set_source_files_properties (merge-swap-reduce.cpp  PROPERTIES COMPILE_FLAGS "/bigobj")
    set_source_files_properties (swap-reduce.cpp        PROPERTIES COMPILE_FLAGS "/bigobj")
    set_source_files_properties (rexchange.cpp          PROPERTIES COMPILE_FLAGS "/bigobj")
    set_source_files_properties (simple.cpp             PROPERTIES COMPILE_FLAGS "/bigobj")
    set_source_files_properties (blobs.cpp              PROPERTIES COMPILE_FLAGS "/bigobj")
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

      add_test              (NAME simple-test-p${p}-b${b}-c
                             COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:simple-test> -b ${b} -c
                            )
      set_tests_properties  (simple-test-p${p}-b${b}-c PROPERTIES TIMEOUT 300 RUN_SERIAL ON)
  endforeach                (p)

  foreach                   (p 2 3)
      math                  (EXPR b "${p} + 1")
      add_test              (NAME blobs-test-p${p}-b${b}
                             COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:blobs-test> -b ${b}
                            )
      set_tests_properties  (blobs-test-p${p}-b${b} PROPERTIES TIMEOUT 300 RUN_SERIAL ON)
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

  foreach                   (p RANGE 1 ${maxp})
          add_test          (NAME double-foreach-p${p}
                             COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:double-foreach>
                            )
  endforeach                (p)

  foreach                   (p 3 4)
          add_test          (NAME collective-balance-test-p${p}
                             COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:collective-balance-test> -i 3
                            )
          add_test          (NAME sampling-balance-test-p${p}
                             COMMAND ${MPIEXEC} -np ${p} $<TARGET_FILE:sampling-balance-test> -i 3
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
