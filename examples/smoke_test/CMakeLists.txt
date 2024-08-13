project(diy_smoke_test CXX)
cmake_minimum_required(VERSION 3.9)
include(CTest)

add_executable(smoke_test block.h opts.h smoke_test.cxx)

if (PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  find_package(DIY REQUIRED)
  target_link_libraries(smoke_test PRIVATE DIY::diy)

  #Only add this test when this a standalone project
  add_test(NAME SmokeTestInternal COMMAND ${CMAKE_BINARY_DIR}/smoke_test)
else()
  target_link_libraries(smoke_test PRIVATE ${libraries})
endif()
