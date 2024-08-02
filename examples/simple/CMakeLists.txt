add_executable              (simple         simple.cpp)
target_link_libraries       (simple         ${libraries})

add_executable              (read-blocks    read-blocks.cpp)
target_link_libraries       (read-blocks    ${libraries})

add_executable              (until-done     until-done.cpp)
target_link_libraries       (until-done     ${libraries})

add_executable              (rexchange      rexchange.cpp)
target_link_libraries       (rexchange      ${libraries})

add_executable              (iexchange-particles      iexchange-particles.cpp)
target_link_libraries       (iexchange-particles      ${libraries})

install                     (TARGETS simple read-blocks until-done rexchange iexchange-particles
                             DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATAROOTDIR}/diy/examples)

if                          (mpi)
    add_executable          (intercomm      intercomm.cpp)
    target_link_libraries   (intercomm      ${libraries})
endif                       (mpi)
