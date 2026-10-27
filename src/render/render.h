#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "buffer/pos.h"
#include "buffer/text_buffer.h"
#include "syntax/syntax.h"
#include "terminal/terminal.h"
#include "window/layout.h"

namespace qalam {

    struct Selection {
        Pos start;
        Pos end;
        bool linewise = false;
        bool block = false;
        std::size_t left = 0;
        std::size_t right = 0;
    };

    struct WindowView {
        const TextBuffer* buffer = nullptr;
        const Syntax* syntax = nullptr;
        Rect rect;
        bool active = false;
        std::string mode;
        std::string fileName;
        bool modified = false;
        bool showWelcome = false;
        bool number = false;
        bool relative = false;
        bool wrap = false;
        bool bidi = false;
        std::optional<Selection> selection;
        std::size_t rowOffset = 0;
        int colOffset = 0;
        std::size_t line = 0;
        std::size_t col = 0;
        int cursorRow = 0;
        int cursorCol = 0;
    };

    struct ViewState {
        std::vector<WindowView> windows;
        std::vector<Rect> separators;
        std::string message;
        std::string searchPattern;
        bool commandLine = false;
        int promptCursor = -1;
    };

    std::string renderFrame(Size size, const ViewState& view);
    int gutterWidth(bool show, std::size_t lineCount, int cols);

}