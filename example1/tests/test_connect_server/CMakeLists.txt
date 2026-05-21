add_executable(test_connect_server
    test_connect_server.cpp
)

add_dependencies(test_launch_server gsync_e1)

target_link_libraries(test_connect_server PRIVATE gsync_e1)

target_compile_definitions(test_connect_server
    PRIVATE
        CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/conf.txt"
)
