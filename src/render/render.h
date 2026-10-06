#pragma once

#include <cstddef>
#include <string>

#include "buffer/text_buffer.h"
#include "terminal/terminal.h"

namespace qalam {

    struct ViewState {
        const TextBuffer* buffer = nullptr;
        std::string mode;
        std::string fileName;
        std::string message;
        std::string searchPattern;
        bool modified = false;
        bool commandLine = false;
        bool showWelcome = false;
        std::size_t rowOffset = 0;
        int colOffset = 0;
        std::size_t line = 0;
        std::size_t col = 0;
        int cursorRow = 0;
        int cursorCol = 0;
    };

    std::string renderFrame(Size size, const ViewState& view);

}