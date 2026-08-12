add_library(
    plate_client_config STATIC
    domain/BootstrapConfig.h
    infrastructure/config/BootstrapConfigLoader.h
    infrastructure/config/BootstrapConfigLoader.cpp
)

plate_client_apply_project_options(plate_client_config)

target_include_directories(
    plate_client_config
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
    plate_client_config
    PUBLIC
        Qt6::Core
)

add_library(
    plate_client_logging STATIC
    infrastructure/logging/TechnicalLogger.h
    infrastructure/logging/TechnicalLogger.cpp
)

plate_client_apply_project_options(plate_client_logging)

target_include_directories(
    plate_client_logging
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
    plate_client_logging
    PUBLIC
        Qt6::Core
)

add_executable(
    plate_client
    app/main.cpp
)

plate_client_apply_project_options(plate_client)

target_link_libraries(plate_client PRIVATE plate_client_config plate_client_logging)

target_link_libraries(
    plate_client
    PRIVATE
        Qt6::Widgets
        Qt6::Network
        Qt6::Concurrent
        Qt6::Mqtt
)
