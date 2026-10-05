#pragma once

#include <string>
#include <vector>

#include "buffer/text_buffer.h"

namespace qalam {

    class LineBuffer : public TextBuffer {
    public:
        LineBuffer();

        std::size_t lineCount() const override;
        const std::string& line(std::size_t index) const override;
        void assign(std::vector<std::string> lines) override;

        void insertChar(std::size_t row, std::size_t col, char c) override;
        void eraseChar(std::size_t row, std::size_t col) override;
        void splitLine(std::size_t row, std::size_t col) override;
        void joinLines(std::size_t row) override;
        void insertLine(std::size_t index, std::string text) override;

    private:
        std::vector<std::string> lines_;
    };

}