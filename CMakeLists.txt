cmake_minimum_required(VERSION 3.30)

project(
    plate_client
    VERSION 0.1.1
    DESCRIPTION "Qt management client for the plate recognition teaching system"
    LANGUAGES CXX
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

string(TIMESTAMP PLATE_CLIENT_BUILD_DATE "%Y-%m-%d")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions)

find_package(
    Qt6 6.11.1 EXACT
    REQUIRED COMPONENTS
        Widgets
        Network
        Concurrent
        Test
)
find_package(
    Qt6Mqtt 6.11.1 EXACT
    CONFIG REQUIRED
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/install/qtmqtt"
    NO_DEFAULT_PATH
)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
