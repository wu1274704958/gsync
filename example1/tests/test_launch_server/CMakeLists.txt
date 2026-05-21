add_executable(test_launch_server
        test_launch_server.cpp
)

add_dependencies(test_launch_server gsync_e1)

target_link_libraries(test_launch_server PUBLIC gsync_e1)

target_compile_definitions(test_launch_server
    PRIVATE
        CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/conf.txt"
)
