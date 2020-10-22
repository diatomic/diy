foreach     (fn
                exchange.py
                iexchange.py
                reduce.py
                mpi.py
                myblock.py
                io-example.py
                pickle-example.py
            )

add_custom_target           (${fn} ALL ${CMAKE_COMMAND} -E copy
                                             ${CMAKE_CURRENT_SOURCE_DIR}/${fn} ${CMAKE_CURRENT_BINARY_DIR}/${fn}
                                             DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${fn})

endforeach()
