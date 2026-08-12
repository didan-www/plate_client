add_executable(
    plate_client
    app/main.cpp
)

plate_client_apply_project_options(plate_client)

target_link_libraries(
    plate_client
    PRIVATE
        Qt6::Widgets
        Qt6::Network
        Qt6::Concurrent
        Qt6::Mqtt
)
