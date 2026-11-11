add_subdirectory(test_launch_server)
add_subdirectory(test_connect_server)

add_executable(test_p2p_session_registry test_p2p_session_registry.cpp ../p2p_session_registry.cpp ../p2p_session.cpp)
target_link_libraries(test_p2p_session_registry PRIVATE mqas ${boringssl_crypto_LIBRARY})
add_test(NAME p2p_session_registry COMMAND test_p2p_session_registry)

add_executable(test_stream_request_ids test_stream_request_ids.cpp)
target_link_libraries(test_stream_request_ids PRIVATE gsync_e1 mqas)
target_compile_definitions(test_stream_request_ids PRIVATE
    CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test_connect_server/conf.txt")
add_executable(test_custom_stream test_custom_stream.cpp)
target_link_libraries(test_custom_stream PRIVATE mqas)
target_compile_definitions(test_custom_stream PRIVATE
    CLIENT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test_connect_server/conf.txt"
    SERVER_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test_launch_server/conf.txt")
add_executable(test_custom_stream_psk test_custom_stream_psk.cpp)
target_include_directories(test_custom_stream_psk PRIVATE ${boringssl_INCLUDE_DIR})
target_link_libraries(test_custom_stream_psk PRIVATE mqas ${boringssl_ssl_LIBRARY} ${boringssl_crypto_LIBRARY})
target_compile_definitions(test_custom_stream_psk PRIVATE
    CLIENT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test_connect_server/conf.txt"
    SERVER_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test_launch_server/conf.txt")
add_executable(test_p2p_example_auth test_p2p_example_auth.cpp)
target_link_libraries(test_p2p_example_auth PRIVATE mqas ${boringssl_crypto_LIBRARY})
target_compile_definitions(test_p2p_example_auth PRIVATE CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test_connect_server/conf.txt")
add_test(NAME p2p_example_auth COMMAND test_p2p_example_auth)
add_executable(test_lobby_registration test_lobby_registration.cpp)
target_link_libraries(test_lobby_registration PRIVATE gsync_e1 mqas)
target_compile_definitions(test_lobby_registration PRIVATE
    CLIENT_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test_connect_server/conf.txt"
    SERVER_CONFIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/test_launch_server/conf.txt")
add_executable(test_p2p_peer test_p2p_peer.cpp)
target_link_libraries(test_p2p_peer PRIVATE gsync_e1)
add_executable(test_lobby_cert_pin test_lobby_cert_pin.cpp)
target_link_libraries(test_lobby_cert_pin PRIVATE gsync_e1)
add_executable(test_peer_c_header test_peer_c_header.c)
target_compile_definitions(test_peer_c_header PRIVATE GSYNC_NO_EXPORT)
target_link_libraries(test_peer_c_header PRIVATE gsync_e1)
add_test(NAME peer_c_header COMMAND test_peer_c_header)

# Compile the migrated examples without requiring their interactive runtimes.
foreach(sample custom_stream_cli send_file pb_stream_cli test_variant_stream_cli test_relay_cli)
    add_library(compile_${sample} OBJECT EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/mqas/examples/${sample}/main.cpp)
    target_link_libraries(compile_${sample} PRIVATE mqas)
endforeach()
find_package(unofficial-pdcurses CONFIG QUIET)
if(TARGET unofficial::pdcurses::pdcurses)
    # Build the standalone mqas samples from the parent workspace as well.
    add_executable(mqas_p2p_sample_client EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/mqas/examples/test_p2p_cli/main.cpp
        ${CMAKE_SOURCE_DIR}/mqas/examples/test_p2p_cli/p2p_chat.cpp
        ${CMAKE_SOURCE_DIR}/mqas/examples/test_p2p_cli/test_p2p.pb.cc)
    target_link_libraries(mqas_p2p_sample_client PRIVATE mqas unofficial::pdcurses::pdcurses ${boringssl_crypto_LIBRARY})
    add_executable(mqas_p2p_sample_server EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/mqas/examples/test_p2p_ser/main.cpp)
    target_link_libraries(mqas_p2p_sample_server PRIVATE mqas)
    foreach(sample test_p2p_cli p2p_voice)
        add_library(compile_${sample} OBJECT EXCLUDE_FROM_ALL
            ${CMAKE_SOURCE_DIR}/mqas/examples/${sample}/main.cpp)
        target_link_libraries(compile_${sample} PRIVATE mqas unofficial::pdcurses::pdcurses)
    endforeach()
    target_include_directories(compile_p2p_voice PRIVATE
        ${CMAKE_SOURCE_DIR}/mqas/third_party/webrtc-audio-processing/webrtc)
endif()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME lobby_cert_pin COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_lobby_cert_pin.py
        $<TARGET_FILE:test_lobby_registration> $<TARGET_FILE:test_lobby_cert_pin>
        ${CMAKE_CURRENT_SOURCE_DIR}/test_connect_server/conf.txt
        ${CMAKE_SOURCE_DIR}/mqas/certificate/cert-def.pem)
    set_tests_properties(lobby_cert_pin PROPERTIES TIMEOUT 35 RUN_SERIAL TRUE)
    add_test(NAME stream_request_ids
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_stream_request_ids.py
        $<TARGET_FILE:test_stream_request_ids> $<TARGET_FILE:test_launch_server>)
    set_tests_properties(stream_request_ids PROPERTIES TIMEOUT 75 RUN_SERIAL TRUE)
    add_test(NAME custom_stream COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_stream_examples.py custom $<TARGET_FILE:test_custom_stream>)
    add_test(NAME custom_stream_psk COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_stream_examples.py custom $<TARGET_FILE:test_custom_stream_psk>)
    set_tests_properties(custom_stream_psk PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    add_test(NAME custom_stream_psk_mismatch COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_custom_stream_psk_mismatch.py $<TARGET_FILE:test_custom_stream_psk>)
    set_tests_properties(custom_stream_psk_mismatch PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    add_test(NAME psk_connection_isolation COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_psk_connection_isolation.py $<TARGET_FILE:test_custom_stream_psk>
        ${CMAKE_CURRENT_SOURCE_DIR}/test_connect_server/conf.txt)
    set_tests_properties(psk_connection_isolation PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    add_test(NAME lobby_registration COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_stream_examples.py lobby $<TARGET_FILE:test_lobby_registration>)
    add_test(NAME p2p_unauthenticated_rejected COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_stream_examples.py p2p
        $<TARGET_FILE:test_launch_server> $<TARGET_FILE:test_connect_server>)
    add_test(NAME p2p_peer COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_p2p_peer.py $<TARGET_FILE:test_lobby_registration>
        $<TARGET_FILE:test_p2p_peer> ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
    foreach(delay server client bad_identity timeout cancel client_cancel bad_config lobby_close parallel)
        add_test(NAME p2p_peer_${delay}_late COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_p2p_peer.py $<TARGET_FILE:test_lobby_registration>
            $<TARGET_FILE:test_p2p_peer> ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR} ${delay})
    endforeach()
    set_tests_properties(custom_stream p2p_unauthenticated_rejected lobby_registration PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    set_tests_properties(p2p_peer p2p_peer_server_late p2p_peer_client_late p2p_peer_bad_identity_late
        p2p_peer_timeout_late p2p_peer_cancel_late p2p_peer_bad_config_late
        p2p_peer_client_cancel_late p2p_peer_lobby_close_late p2p_peer_parallel_late PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
endif()
