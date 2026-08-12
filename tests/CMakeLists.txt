add_executable(
    tst_smoke
    tst_smoke.cpp
)

plate_client_apply_project_options(tst_smoke)

target_link_libraries(
    tst_smoke
    PRIVATE
        Qt6::Widgets
        Qt6::Network
        Qt6::Concurrent
        Qt6::Mqtt
        Qt6::Test
)

add_test(NAME unit.smoke COMMAND tst_smoke)
add_test(NAME app.startup COMMAND $<TARGET_FILE:plate_client> --smoke-test)

set_tests_properties(
    unit.smoke
    app.startup
    PROPERTIES
        TIMEOUT 10
)
