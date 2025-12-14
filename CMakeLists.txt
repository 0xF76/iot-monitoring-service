cmake_minimum_required(VERSION 3.10)

project(iot-monitor VERSION 0.1.0 LANGUAGES C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(protocol
    src/common/protocol.c
)

target_include_directories(protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

# Server
add_executable(server
    src/server/main.c
    src/server/server.c
)

target_link_libraries(server protocol)

# Client
add_executable(client
    src/client/main.c
    src/client/client.c
)

target_link_libraries(client protocol)