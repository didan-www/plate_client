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

plate_client_apply_project_options(tst_smoke)
plate_client_apply_project_options(tst_bootstrap_config)
plate_client_apply_project_options(tst_technical_logger)

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

add_test(NAME unit.smoke COMMAND tst_smoke)
add_test(NAME unit.bootstrap_config COMMAND tst_bootstrap_config)
add_test(NAME unit.technical_logger COMMAND tst_technical_logger)
add_test(NAME app.startup COMMAND $<TARGET_FILE:plate_client> --smoke-test)

set_tests_properties(
    unit.smoke
    unit.bootstrap_config
    unit.technical_logger
    app.startup
    PROPERTIES
        TIMEOUT 10
)
