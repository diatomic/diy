add_executable              (simple simple.cpp)
target_link_libraries       (simple     ${libraries})

add_executable              (read-blocks read-blocks.cpp)
target_link_libraries       (read-blocks     ${libraries})

add_executable              (until-done until-done.cpp)
target_link_libraries       (until-done     ${libraries})
