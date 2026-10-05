#include "buffer/line_buffer.h"

#include <cstddef>
#include <utility>

namespace qalam {

    LineBuffer::LineBuffer() : lines_(1) {}

    std::size_t LineBuffer::lineCount() const {
        return lines_.size();
    }

    const std::string& LineBuffer::line(std::size_t index) const {
        return lines_[index];
    }

    void LineBuffer::assign(std::vector<std::string> lines) {
        lines_ = std::move(lines);
        if (lines_.empty()) lines_.emplace_back();
    }

    void LineBuffer::insertChar(std::size_t row, std::size_t col, char c) {
        lines_[row].insert(col, 1, c);
    }

    void LineBuffer::eraseChar(std::size_t row, std::size_t col) {
        lines_[row].erase(col, 1);
    }

    void LineBuffer::splitLine(std::size_t row, std::size_t col) {
        std::string rest = lines_[row].substr(col);
        lines_[row].erase(col);
        lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(row) + 1, std::move(rest));
    }

    void LineBuffer::joinLines(std::size_t row) {
        lines_[row] += lines_[row + 1];
        lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(row) + 1);
    }

    void LineBuffer::insertLine(std::size_t index, std::string text) {
        lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(index), std::move(text));
    }

}