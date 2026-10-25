cmake_minimum_required(VERSION 3.20)
project(qalam VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif ()

option(QALAM_BUILD_TESTS "Build the unit tests" ON)
option(QALAM_BUILD_BENCH "Build the benchmark" OFF)
option(QALAM_SANITIZE "Build with AddressSanitizer and UBSan" OFF)

file(GLOB_RECURSE QALAM_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM QALAM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

function(qalam_apply_flags target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if (QALAM_SANITIZE)
        target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-sanitize-recover=undefined
                -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif ()
endfunction()

add_library(qalam_core STATIC ${QALAM_SOURCES})
target_include_directories(qalam_core PUBLIC src)
target_compile_features(qalam_core PUBLIC cxx_std_20)
qalam_apply_flags(qalam_core)

add_executable(qalam src/main.cpp)
target_link_libraries(qalam PRIVATE qalam_core)
qalam_apply_flags(qalam)

install(TARGETS qalam RUNTIME DESTINATION bin)

if (QALAM_BUILD_TESTS)
    enable_testing()

    add_executable(qalam_tests
            tests/test_main.cpp
            tests/test_text.cpp
            tests/test_buffer.cpp
            tests/test_line_buffer.cpp
            tests/test_regex.cpp
            tests/test_motions.cpp
            tests/test_syntax.cpp
            tests/test_editor.cpp
            tests/test_config.cpp
            tests/test_layout.cpp
            tests/test_render.cpp
            tests/test_file_io.cpp
            tests/test_decoder.cpp
            tests/test_paste.cpp)
    target_include_directories(qalam_tests PRIVATE tests)
    target_link_libraries(qalam_tests PRIVATE qalam_core)
    qalam_apply_flags(qalam_tests)

    add_test(NAME unit COMMAND qalam_tests)
endif ()

if (QALAM_BUILD_BENCH)
    add_executable(qalam_bench bench/bench.cpp)
    target_link_libraries(qalam_bench PRIVATE qalam_core)
endif ()