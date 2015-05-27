add_executable              (swap-reduce swap-reduce.cpp)
target_link_libraries       (swap-reduce ${libraries})

add_executable              (merge-reduce merge-reduce.cpp)
target_link_libraries       (merge-reduce ${libraries})

add_executable              (kd-tree kd-tree.cpp)
target_link_libraries       (kd-tree ${libraries})

add_executable              (sort sort.cpp)
target_link_libraries       (sort ${libraries})

add_executable              (all-to-all all-to-all.cpp)
target_link_libraries       (all-to-all ${libraries})
