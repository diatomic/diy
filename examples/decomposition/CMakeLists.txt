add_executable          (test-decomposition test-decomposition.cpp)
add_executable          (test-direct-master test-direct-master.cpp)
target_link_libraries   (test-direct-master ${libraries})
