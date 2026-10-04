#include "buffer/line_buffer.h"

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

}