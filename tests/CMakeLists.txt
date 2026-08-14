add_executable(
    tst_smoke
    tst_smoke.cpp
)

add_executable(
    tst_bootstrap_config
    config/tst_bootstrap_config.cpp
)

add_executable(
    tst_technical_logger
    logging/tst_technical_logger.cpp
)

add_executable(
    tst_domain_types
    domain/tst_domain_types.cpp
)

add_executable(
    tst_json_codecs
    http/tst_json_codecs.cpp
)

add_executable(
    tst_http_client
    http/tst_http_client.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_auth_api_client
    http/tst_auth_api_client.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_session_service
    services/tst_session_service.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_login_window
    ui/tst_login_window.cpp
)

add_executable(
    tst_app_controller_login
    app/tst_app_controller_login.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_application_startup
    app/tst_application_startup.cpp
)

plate_client_apply_project_options(tst_smoke)
plate_client_apply_project_options(tst_bootstrap_config)
plate_client_apply_project_options(tst_technical_logger)
plate_client_apply_project_options(tst_domain_types)
plate_client_apply_project_options(tst_json_codecs)
plate_client_apply_project_options(tst_http_client)
plate_client_apply_project_options(tst_auth_api_client)
plate_client_apply_project_options(tst_session_service)
plate_client_apply_project_options(tst_login_window)
plate_client_apply_project_options(tst_app_controller_login)
plate_client_apply_project_options(tst_application_startup)

target_link_libraries(
    tst_smoke
    PRIVATE
        Qt6::Widgets
        Qt6::Network
        Qt6::Concurrent
        Qt6::Mqtt
        Qt6::Test
)

target_link_libraries(
    tst_bootstrap_config
    PRIVATE
        plate_client_config
        Qt6::Test
)

target_link_libraries(
    tst_technical_logger
    PRIVATE
        plate_client_logging
        Qt6::Test
)

target_link_libraries(
    tst_domain_types
    PRIVATE
        plate_client_protocol
        Qt6::Test
)

target_link_libraries(
    tst_json_codecs
    PRIVATE
        plate_client_protocol
        Qt6::Test
)

target_include_directories(tst_http_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_http_client
    PRIVATE
        plate_client_http
        Qt6::Network
        Qt6::Test
)

target_include_directories(tst_auth_api_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_auth_api_client
    PRIVATE
        plate_client_auth
        Qt6::Network
        Qt6::Test
)

target_include_directories(tst_session_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_session_service
    PRIVATE
        plate_client_auth
        Qt6::Network
        Qt6::Test
)

target_link_libraries(
    tst_login_window
    PRIVATE
        plate_client_ui
        Qt6::Test
)

target_include_directories(tst_app_controller_login PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_app_controller_login
    PRIVATE
        plate_client_app
        Qt6::Network
        Qt6::Test
)

target_link_libraries(
    tst_application_startup
    PRIVATE
        plate_client_app
        Qt6::Test
)

add_test(NAME unit.smoke COMMAND tst_smoke)
add_test(NAME unit.bootstrap_config COMMAND tst_bootstrap_config)
add_test(NAME unit.technical_logger COMMAND tst_technical_logger)
add_test(NAME unit.domain_types COMMAND tst_domain_types)
add_test(NAME unit.json_codecs COMMAND tst_json_codecs)
add_test(NAME unit.http_client COMMAND tst_http_client)
add_test(NAME unit.auth_api_client COMMAND tst_auth_api_client)
add_test(NAME unit.session_service COMMAND tst_session_service)
add_test(NAME unit.login_window COMMAND tst_login_window)
add_test(NAME unit.login_window.dpi100 COMMAND tst_login_window)
add_test(NAME unit.login_window.dpi150 COMMAND tst_login_window)
add_test(NAME unit.login_window.dpi200 COMMAND tst_login_window)
add_test(NAME unit.app_controller_login COMMAND tst_app_controller_login)
add_test(NAME unit.application_startup COMMAND tst_application_startup)
add_test(NAME app.startup COMMAND $<TARGET_FILE:plate_client> --smoke-test)

set_tests_properties(
    unit.smoke
    unit.bootstrap_config
    unit.technical_logger
    unit.domain_types
    unit.json_codecs
    unit.http_client
    unit.auth_api_client
    unit.session_service
    unit.login_window
    unit.login_window.dpi100
    unit.login_window.dpi150
    unit.login_window.dpi200
    unit.app_controller_login
    unit.application_startup
    app.startup
    PROPERTIES
        TIMEOUT 10
)

set_tests_properties(
    unit.login_window.dpi100
    PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=1"
)

set_tests_properties(
    unit.login_window.dpi150
    PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=1.5"
)

set_tests_properties(
    unit.login_window.dpi200
    PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=2"
)
