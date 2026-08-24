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
    tst_heartbeat_service
    services/tst_heartbeat_service.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_mqtt_subscriber
    mqtt/tst_mqtt_subscriber.cpp
    support/MockMqttClient.h
)

add_executable(
    tst_recognition_event_codec
    mqtt/tst_recognition_event_codec.cpp
)

add_executable(
    tst_realtime_store
    services/tst_realtime_store.cpp
)

add_executable(
    tst_recognition_api_detail
    http/tst_recognition_api_detail.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_recognition_api_image
    http/tst_recognition_api_image.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_recognition_api_history
    http/tst_recognition_api_history.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_recognition_api_csv
    http/tst_recognition_api_csv.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_history_service
    services/tst_history_service.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_access_list_api
    http/tst_access_list_api.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_access_list_service
    services/tst_access_list_service.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_csv_download_service
    services/tst_csv_download_service.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_image_load_service
    services/tst_image_load_service.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_image_decode_limits
    services/tst_image_decode_limits.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_reconnect_reconciler
    services/tst_reconnect_reconciler.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
    support/MockMqttClient.h
)

add_executable(
    tst_login_window
    ui/tst_login_window.cpp
)

add_executable(
    tst_main_window_shell
    ui/tst_main_window_shell.cpp
)

add_executable(
    tst_http_status_indicator
    ui/tst_http_status_indicator.cpp
)

add_executable(
    tst_recognition_table_model
    ui/tst_recognition_table_model.cpp
)

add_executable(
    tst_realtime_page
    ui/tst_realtime_page.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_history_page
    ui/tst_history_page.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_history_export
    ui/tst_history_export.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_access_list_dialog
    ui/tst_access_list_dialog.cpp
)

add_executable(
    tst_access_list_page
    ui/tst_access_list_page.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    plate_client_main_window_preview EXCLUDE_FROM_ALL
    ui/main_window_preview.cpp
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
    support/MockHttpServer.h
    support/MockHttpServer.cpp
    support/MockMqttClient.h
)

add_executable(
    tst_logout_lifecycle
    app/tst_logout_lifecycle.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_async_destruction
    app/tst_async_destruction.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
)

add_executable(
    tst_integration_realtime_flow
    integration/tst_realtime_flow.cpp
    integration/MockPlateServer.h
    integration/MockPlateServer.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
    support/MockMqttClient.h
)

add_executable(
    tst_integration_history_and_csv_flow
    integration/tst_history_and_csv_flow.cpp
    integration/MockPlateServer.h
    integration/MockPlateServer.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
    support/MockMqttClient.h
)

add_executable(
    tst_integration_access_list_flow
    integration/tst_access_list_flow.cpp
    integration/MockPlateServer.h
    integration/MockPlateServer.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
    support/MockMqttClient.h
)

add_executable(
    tst_integration_independent_connectivity
    integration/tst_independent_connectivity.cpp
    integration/MockPlateServer.h
    integration/MockPlateServer.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
    support/MockMqttClient.h
)

add_executable(
    tst_realtime_throughput
    performance/tst_realtime_throughput.cpp
)

add_executable(
    tst_responsive_layout
    ui/tst_responsive_layout.cpp
    integration/MockPlateServer.h
    integration/MockPlateServer.cpp
    support/MockHttpServer.h
    support/MockHttpServer.cpp
    support/MockMqttClient.h
)

add_executable(
    tst_local_data_boundary
    security/tst_local_data_boundary.cpp
)

plate_client_apply_project_options(tst_smoke)
plate_client_apply_project_options(tst_bootstrap_config)
plate_client_apply_project_options(tst_technical_logger)
plate_client_apply_project_options(tst_domain_types)
plate_client_apply_project_options(tst_json_codecs)
plate_client_apply_project_options(tst_http_client)
plate_client_apply_project_options(tst_auth_api_client)
plate_client_apply_project_options(tst_session_service)
plate_client_apply_project_options(tst_heartbeat_service)
plate_client_apply_project_options(tst_mqtt_subscriber)
plate_client_apply_project_options(tst_recognition_event_codec)
plate_client_apply_project_options(tst_realtime_store)
plate_client_apply_project_options(tst_recognition_api_detail)
plate_client_apply_project_options(tst_recognition_api_image)
plate_client_apply_project_options(tst_recognition_api_history)
plate_client_apply_project_options(tst_recognition_api_csv)
plate_client_apply_project_options(tst_history_service)
plate_client_apply_project_options(tst_access_list_api)
plate_client_apply_project_options(tst_access_list_service)
plate_client_apply_project_options(tst_csv_download_service)
plate_client_apply_project_options(tst_image_load_service)
plate_client_apply_project_options(tst_image_decode_limits)
plate_client_apply_project_options(tst_reconnect_reconciler)
plate_client_apply_project_options(tst_login_window)
plate_client_apply_project_options(tst_main_window_shell)
plate_client_apply_project_options(tst_http_status_indicator)
plate_client_apply_project_options(tst_recognition_table_model)
plate_client_apply_project_options(tst_realtime_page)
plate_client_apply_project_options(tst_history_page)
plate_client_apply_project_options(tst_history_export)
plate_client_apply_project_options(tst_access_list_dialog)
plate_client_apply_project_options(tst_access_list_page)
plate_client_apply_project_options(plate_client_main_window_preview)
plate_client_apply_project_options(tst_app_controller_login)
plate_client_apply_project_options(tst_application_startup)
plate_client_apply_project_options(tst_logout_lifecycle)
plate_client_apply_project_options(tst_async_destruction)
plate_client_apply_project_options(tst_integration_realtime_flow)
plate_client_apply_project_options(tst_integration_history_and_csv_flow)
plate_client_apply_project_options(tst_integration_access_list_flow)
plate_client_apply_project_options(tst_integration_independent_connectivity)
plate_client_apply_project_options(tst_realtime_throughput)
plate_client_apply_project_options(tst_responsive_layout)
plate_client_apply_project_options(tst_local_data_boundary)

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

target_include_directories(tst_heartbeat_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_heartbeat_service
    PRIVATE
        plate_client_heartbeat
        Qt6::Network
        Qt6::Test
)

target_include_directories(tst_mqtt_subscriber PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_mqtt_subscriber
    PRIVATE
        plate_client_mqtt
        Qt6::Test
)

target_link_libraries(
    tst_recognition_event_codec
    PRIVATE
        plate_client_mqtt
        Qt6::Test
)

target_link_libraries(
    tst_realtime_store
    PRIVATE
        plate_client_realtime
        Qt6::Test
)

target_include_directories(tst_recognition_api_detail PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_recognition_api_detail
    PRIVATE
        plate_client_reconciliation
        Qt6::Network
        Qt6::Test
)

target_include_directories(tst_recognition_api_image PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_recognition_api_image
    PRIVATE
        plate_client_reconciliation
        Qt6::Network
        Qt6::Test
)

target_include_directories(tst_recognition_api_history PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_recognition_api_history
    PRIVATE
        plate_client_reconciliation
        Qt6::Network
        Qt6::Test
)

target_include_directories(tst_recognition_api_csv PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_recognition_api_csv
    PRIVATE
        plate_client_reconciliation
        Qt6::Network
        Qt6::Test
)

target_include_directories(tst_history_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_history_service
    PRIVATE
        plate_client_history
        Qt6::Network
        Qt6::Test
)

target_include_directories(tst_access_list_api PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_access_list_api
    PRIVATE
        plate_client_access_list
        Qt6::Network
        Qt6::Test
)

target_include_directories(tst_access_list_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_access_list_service
    PRIVATE
        plate_client_access_list
        Qt6::Network
        Qt6::Test
)

target_include_directories(tst_csv_download_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_csv_download_service
    PRIVATE
        plate_client_history
        Qt6::Network
        Qt6::Test
)

target_include_directories(tst_image_load_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_image_load_service
    PRIVATE
        plate_client_reconciliation
        Qt6::Network
        Qt6::Test
)

target_include_directories(tst_image_decode_limits PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_image_decode_limits
    PRIVATE
        plate_client_reconciliation
        Qt6::Network
        Qt6::Test
        Qt6::Widgets
)

target_include_directories(tst_reconnect_reconciler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_reconnect_reconciler
    PRIVATE
        plate_client_reconciliation
        Qt6::Network
        Qt6::Test
)

target_link_libraries(
    tst_login_window
    PRIVATE
        plate_client_ui
        Qt6::Test
)

target_link_libraries(
    tst_main_window_shell
    PRIVATE
        plate_client_ui
        Qt6::Test
)

target_link_libraries(
    tst_http_status_indicator
    PRIVATE
        plate_client_ui
        Qt6::Test
)

target_link_libraries(
    tst_recognition_table_model
    PRIVATE
        plate_client_ui
        Qt6::Test
)

target_include_directories(tst_realtime_page PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_realtime_page
    PRIVATE
        plate_client_ui
        Qt6::Network
        Qt6::Test
)

target_include_directories(tst_history_page PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_history_page
    PRIVATE
        plate_client_ui
        Qt6::Network
        Qt6::Test
)

target_include_directories(tst_history_export PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_history_export
    PRIVATE
        plate_client_ui
        Qt6::Network
        Qt6::Test
)

target_link_libraries(
    tst_access_list_dialog
    PRIVATE
        plate_client_ui
        Qt6::Test
)

target_include_directories(tst_access_list_page PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_access_list_page
    PRIVATE
        plate_client_ui
        Qt6::Network
        Qt6::Test
)

target_link_libraries(
    plate_client_main_window_preview
    PRIVATE
        plate_client_ui
        Qt6::Widgets
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
        Qt6::Network
        Qt6::Test
)

target_include_directories(tst_application_startup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(tst_logout_lifecycle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_logout_lifecycle
    PRIVATE
        plate_client_app
        Qt6::Network
        Qt6::Test
)

target_include_directories(tst_async_destruction PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_async_destruction
    PRIVATE
        plate_client_app
        Qt6::Network
        Qt6::Test
)

foreach(target
        tst_integration_realtime_flow
        tst_integration_history_and_csv_flow
        tst_integration_access_list_flow
        tst_integration_independent_connectivity)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(
        ${target}
        PRIVATE
            plate_client_app
            Qt6::Network
            Qt6::Test
    )
endforeach()

target_link_libraries(
    tst_realtime_throughput
    PRIVATE
        plate_client_ui
        Qt6::Test
)

target_include_directories(tst_responsive_layout PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    tst_responsive_layout
    PRIVATE
        plate_client_app
        Qt6::Network
        Qt6::Test
)

target_link_libraries(
    tst_local_data_boundary
    PRIVATE
        plate_client_logging
        Qt6::Test
)

target_compile_definitions(
    tst_local_data_boundary
    PRIVATE
        PLATE_CLIENT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        PLATE_CLIENT_APP_PATH="$<TARGET_FILE:plate_client>"
)

add_dependencies(tst_local_data_boundary plate_client)

add_test(NAME unit.smoke COMMAND tst_smoke)
add_test(NAME unit.bootstrap_config COMMAND tst_bootstrap_config)
add_test(NAME unit.technical_logger COMMAND tst_technical_logger)
add_test(NAME unit.domain_types COMMAND tst_domain_types)
add_test(NAME unit.json_codecs COMMAND tst_json_codecs)
add_test(NAME unit.http_client COMMAND tst_http_client)
add_test(NAME unit.auth_api_client COMMAND tst_auth_api_client)
add_test(NAME unit.session_service COMMAND tst_session_service)
add_test(NAME unit.heartbeat_service COMMAND tst_heartbeat_service)
add_test(NAME unit.mqtt_subscriber COMMAND tst_mqtt_subscriber)
add_test(NAME unit.recognition_event_codec COMMAND tst_recognition_event_codec)
add_test(NAME unit.realtime_store COMMAND tst_realtime_store)
add_test(NAME unit.recognition_api_detail COMMAND tst_recognition_api_detail)
add_test(NAME unit.recognition_api_image COMMAND tst_recognition_api_image)
add_test(NAME unit.recognition_api_history COMMAND tst_recognition_api_history)
add_test(NAME unit.recognition_api_csv COMMAND tst_recognition_api_csv)
add_test(NAME unit.history_service COMMAND tst_history_service)
add_test(NAME unit.access_list_api COMMAND tst_access_list_api)
add_test(NAME unit.access_list_service COMMAND tst_access_list_service)
add_test(NAME unit.csv_download_service COMMAND tst_csv_download_service)
add_test(NAME unit.image_load_service COMMAND tst_image_load_service)
add_test(NAME unit.image_decode_limits COMMAND tst_image_decode_limits)
add_test(NAME unit.reconnect_reconciler COMMAND tst_reconnect_reconciler)
add_test(NAME unit.login_window COMMAND tst_login_window)
add_test(NAME unit.login_window.dpi100 COMMAND tst_login_window)
add_test(NAME unit.login_window.dpi150 COMMAND tst_login_window)
add_test(NAME unit.login_window.dpi200 COMMAND tst_login_window)
add_test(NAME unit.main_window_shell COMMAND tst_main_window_shell)
add_test(NAME unit.main_window_shell.dpi100 COMMAND tst_main_window_shell)
add_test(NAME unit.main_window_shell.dpi150 COMMAND tst_main_window_shell)
add_test(NAME unit.main_window_shell.dpi200 COMMAND tst_main_window_shell)
add_test(NAME unit.http_status_indicator COMMAND tst_http_status_indicator)
add_test(NAME unit.recognition_table_model COMMAND tst_recognition_table_model)
add_test(NAME unit.realtime_page COMMAND tst_realtime_page)
add_test(NAME unit.realtime_page.dpi100 COMMAND tst_realtime_page)
add_test(NAME unit.realtime_page.dpi150 COMMAND tst_realtime_page)
add_test(NAME unit.realtime_page.dpi200 COMMAND tst_realtime_page)
add_test(NAME unit.history_page COMMAND tst_history_page)
add_test(NAME unit.history_page.dpi100 COMMAND tst_history_page)
add_test(NAME unit.history_page.dpi150 COMMAND tst_history_page)
add_test(NAME unit.history_page.dpi200 COMMAND tst_history_page)
add_test(NAME unit.history_export COMMAND tst_history_export)
add_test(NAME unit.access_list_dialog COMMAND tst_access_list_dialog)
add_test(NAME unit.access_list_page COMMAND tst_access_list_page)
add_test(NAME unit.access_list_page.dpi100 COMMAND tst_access_list_page)
add_test(NAME unit.access_list_page.dpi150 COMMAND tst_access_list_page)
add_test(NAME unit.access_list_page.dpi200 COMMAND tst_access_list_page)
add_test(NAME unit.app_controller_login COMMAND tst_app_controller_login)
add_test(NAME unit.application_startup COMMAND tst_application_startup)
add_test(NAME unit.logout_lifecycle COMMAND tst_logout_lifecycle)
add_test(NAME unit.async_destruction COMMAND tst_async_destruction)
add_test(NAME app.startup COMMAND $<TARGET_FILE:plate_client> --smoke-test)
add_test(NAME integration.realtime_flow COMMAND tst_integration_realtime_flow)
add_test(NAME integration.history_and_csv_flow COMMAND tst_integration_history_and_csv_flow)
add_test(NAME integration.access_list_flow COMMAND tst_integration_access_list_flow)
add_test(NAME integration.independent_connectivity COMMAND tst_integration_independent_connectivity)
add_test(NAME performance.realtime_throughput COMMAND tst_realtime_throughput)
add_test(NAME ui.responsive_layout.dpi100 COMMAND tst_responsive_layout)
add_test(NAME ui.responsive_layout.dpi150 COMMAND tst_responsive_layout)
add_test(NAME ui.responsive_layout.dpi200 COMMAND tst_responsive_layout)
add_test(NAME security.local_data_boundary COMMAND tst_local_data_boundary)

set_tests_properties(
    unit.smoke
    unit.bootstrap_config
    unit.technical_logger
    unit.domain_types
    unit.json_codecs
    unit.http_client
    unit.auth_api_client
    unit.session_service
    unit.heartbeat_service
    unit.mqtt_subscriber
    unit.recognition_event_codec
    unit.realtime_store
    unit.recognition_api_detail
    unit.recognition_api_image
    unit.recognition_api_history
    unit.recognition_api_csv
    unit.history_service
    unit.access_list_api
    unit.access_list_service
    unit.csv_download_service
    unit.image_load_service
    unit.image_decode_limits
    unit.reconnect_reconciler
    unit.login_window
    unit.login_window.dpi100
    unit.login_window.dpi150
    unit.login_window.dpi200
    unit.main_window_shell
    unit.main_window_shell.dpi100
    unit.main_window_shell.dpi150
    unit.main_window_shell.dpi200
    unit.http_status_indicator
    unit.recognition_table_model
    unit.realtime_page
    unit.realtime_page.dpi100
    unit.realtime_page.dpi150
    unit.realtime_page.dpi200
    unit.history_page
    unit.history_page.dpi100
    unit.history_page.dpi150
    unit.history_page.dpi200
    unit.history_export
    unit.access_list_dialog
    unit.access_list_page
    unit.access_list_page.dpi100
    unit.access_list_page.dpi150
    unit.access_list_page.dpi200
    unit.app_controller_login
    unit.application_startup
    unit.logout_lifecycle
    unit.async_destruction
    app.startup
    integration.realtime_flow
    integration.history_and_csv_flow
    integration.access_list_flow
    integration.independent_connectivity
    PROPERTIES
        TIMEOUT 10
)

set_tests_properties(
    integration.realtime_flow
    integration.history_and_csv_flow
    integration.access_list_flow
    integration.independent_connectivity
    PROPERTIES
        LABELS integration
        TIMEOUT 15
)

set_tests_properties(
    performance.realtime_throughput
    PROPERTIES
        LABELS performance
        RUN_SERIAL TRUE
        TIMEOUT 125
)

set_tests_properties(
    ui.responsive_layout.dpi100
    ui.responsive_layout.dpi150
    ui.responsive_layout.dpi200
    PROPERTIES
        LABELS ui-acceptance
        TIMEOUT 30
)

set_tests_properties(
    security.local_data_boundary
    PROPERTIES
        LABELS security
        TIMEOUT 10
)

set_tests_properties(
    unit.login_window.dpi100
    unit.main_window_shell.dpi100
    unit.realtime_page.dpi100
    unit.history_page.dpi100
    unit.access_list_page.dpi100
    ui.responsive_layout.dpi100
    PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=1"
)

set_tests_properties(
    unit.login_window.dpi150
    unit.main_window_shell.dpi150
    unit.realtime_page.dpi150
    unit.history_page.dpi150
    unit.access_list_page.dpi150
    ui.responsive_layout.dpi150
    PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=1.5"
)

set_tests_properties(
    unit.login_window.dpi200
    unit.main_window_shell.dpi200
    unit.realtime_page.dpi200
    unit.history_page.dpi200
    unit.access_list_page.dpi200
    ui.responsive_layout.dpi200
    PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=2"
)
