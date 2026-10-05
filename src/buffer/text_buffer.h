#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "buffer/edit.h"
#include "buffer/pos.h"

namespace qalam {

    class TextBuffer {
    public:
        virtual ~TextBuffer() = default;

        virtual std::size_t lineCount() const = 0;
        virtual const std::string& line(std::size_t index) const = 0;
        virtual void assign(std::vector<std::string> lines) = 0;

        virtual void insertChar(std::size_t row, std::size_t col, char c) = 0;
        virtual void eraseChar(std::size_t row, std::size_t col) = 0;
        virtual void splitLine(std::size_t row, std::size_t col) = 0;
        virtual void joinLines(std::size_t row) = 0;
        virtual void insertLine(std::size_t index, std::string text) = 0;

        virtual std::string text(Pos start, Pos end) const = 0;
        virtual Edit replace(Pos start, Pos end, const std::string& inserted) = 0;
    };

}