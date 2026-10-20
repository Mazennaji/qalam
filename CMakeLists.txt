cmake_minimum_required(VERSION 3.20)
project(qalam VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif ()

add_executable(qalam
        src/main.cpp
        src/terminal/terminal.cpp
        src/terminal/terminal.h
        src/text/columns.cpp
        src/text/columns.h
        src/buffer/pos.h
        src/buffer/edit.h
        src/buffer/text_buffer.h
        src/buffer/line_buffer.cpp
        src/buffer/line_buffer.h
        src/buffer/file_io.cpp
        src/buffer/file_io.h
        src/motions/motions.cpp
        src/motions/motions.h
        src/search/search.cpp
        src/search/search.h
        src/search/substitute.cpp
        src/search/substitute.h
        src/undo/history.cpp
        src/undo/history.h
        src/input/key_notation.cpp
        src/input/key_notation.h
        src/input/keymap.cpp
        src/input/keymap.h
        src/config/config.cpp
        src/config/config.h
        src/render/render.cpp
        src/render/render.h
        src/editor/editor.cpp
        src/editor/editor.h
        src/syntax/syntax.cpp
        src/syntax/syntax.h
        src/motions/objects.cpp
        src/motions/objects.h
        src/window/layout.cpp
        src/window/layout.h
        src/search/regex.cpp
        src/search/regex.h
        src/clipboard/clipboard.cpp
        src/clipboard/clipboard.h
)

option(QALAM_BUILD_TESTS "Build the unit tests" ON)

if (QALAM_BUILD_TESTS)
    enable_testing()

    add_executable(qalam_tests
            tests/test_main.cpp
            tests/test_text.cpp
            tests/test_buffer.cpp
            tests/test_regex.cpp
            tests/test_motions.cpp
            tests/test_syntax.cpp
            src/text/columns.cpp
            src/buffer/line_buffer.cpp
            src/undo/history.cpp
            src/search/regex.cpp
            src/search/search.cpp
            src/search/substitute.cpp
            src/motions/motions.cpp
            src/motions/objects.cpp
            src/syntax/syntax.cpp)

    target_include_directories(qalam_tests PRIVATE src tests)
    target_compile_features(qalam_tests PRIVATE cxx_std_20)

    add_test(NAME unit COMMAND qalam_tests)
endif ()

target_include_directories(qalam PRIVATE src)
target_compile_options(qalam PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS qalam RUNTIME DESTINATION bin)