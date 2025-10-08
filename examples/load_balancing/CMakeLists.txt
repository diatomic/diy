set                         (targets    collective
                                        sampling
                                        none)

foreach                     (t ${targets})
  add_executable            (${t} ${t}.cpp)
  target_link_libraries     (${t} ${libraries})
endforeach                  ()

# dynamic balance example requires threads
if                          (threads)
  add_executable            (dynamic dynamic.cpp)
  target_link_libraries     (dynamic ${libraries})
endif()
