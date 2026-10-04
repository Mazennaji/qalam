cmake_minimum_required(VERSION 3.20)
project(qalam VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(qalam
        src/main.cpp
        src/terminal/terminal.cpp
        src/terminal/terminal.h
)

target_include_directories(qalam PRIVATE src)
target_compile_options(qalam PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS qalam RUNTIME DESTINATION bin)