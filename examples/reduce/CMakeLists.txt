set                         (targets    swap-reduce
                                        merge-reduce
                                        kd-tree
                                        sort
                                        sample-sort
                                        all-to-all
                                        all-done)

foreach                     (t ${targets})
  add_executable            (${t} ${t}.cpp)
  target_link_libraries     (${t} ${libraries})
endforeach                  ()
