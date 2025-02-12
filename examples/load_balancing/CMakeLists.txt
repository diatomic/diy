set                         (targets    collective
                                        sampling
                                        none
                                        dynamic)

foreach                     (t ${targets})
  add_executable            (${t} ${t}.cpp)
  target_link_libraries     (${t} ${libraries})
endforeach                  ()
