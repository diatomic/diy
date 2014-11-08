add_executable              (swap-reduce swap-reduce.cpp)
target_link_libraries       (swap-reduce ${libraries})

add_executable              (merge-reduce merge-reduce.cpp)
target_link_libraries       (merge-reduce ${libraries})
