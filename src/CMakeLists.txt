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

add_library(
    plate_client_protocol STATIC
    domain/SessionContext.h
    domain/RecognitionSnapshot.h
    domain/AccessListRecord.h
    domain/RequestContext.h
    domain/PageResult.h
    domain/HttpEnvelope.h
    domain/Errors.h
    infrastructure/http/JsonCodecs.h
    infrastructure/http/JsonCodecs.cpp
)

add_library(
    plate_client_http STATIC
    infrastructure/http/HttpRequestPolicy.h
    infrastructure/http/HttpError.h
    infrastructure/http/HttpClient.h
    infrastructure/http/HttpClient.cpp
)

add_library(
    plate_client_auth STATIC
    infrastructure/http/AuthError.h
    infrastructure/http/AuthApiClient.h
    infrastructure/http/AuthApiClient.cpp
    services/SessionService.h
    services/SessionService.cpp
)

plate_client_apply_project_options(plate_client_http)

target_include_directories(
    plate_client_http
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
    plate_client_http
    PUBLIC
        Qt6::Core
        Qt6::Network
)

plate_client_apply_project_options(plate_client_auth)

target_include_directories(
    plate_client_auth
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
    plate_client_auth
    PUBLIC
        plate_client_http
        plate_client_protocol
        Qt6::Core
)

plate_client_apply_project_options(plate_client_protocol)

target_include_directories(
    plate_client_protocol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
    plate_client_protocol
    PUBLIC
        Qt6::Core
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
