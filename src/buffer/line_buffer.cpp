#include "buffer/line_buffer.h"

#include <cstddef>
#include <iterator>
#include <utility>

namespace qalam {
namespace {

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> pieces;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = text.find('\n', begin);
        if (end == std::string::npos) {
            pieces.push_back(text.substr(begin));
            break;
        }
        pieces.push_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
    return pieces;
}

}

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

std::string LineBuffer::text(Pos start, Pos end) const {
    if (start.row == end.row) {
        return lines_[start.row].substr(start.col, end.col - start.col);
    }

    std::string out = lines_[start.row].substr(start.col);
    for (std::size_t r = start.row + 1; r < end.row; ++r) {
        out += '\n';
        out += lines_[r];
    }
    out += '\n';
    out += lines_[end.row].substr(0, end.col);
    return out;
}

Edit LineBuffer::replace(Pos start, Pos end, const std::string& inserted) {
    Edit edit{start, text(start, end), inserted};

    std::vector<std::string> pieces = splitLines(inserted);
    pieces.front() = lines_[start.row].substr(0, start.col) + pieces.front();
    pieces.back() += lines_[end.row].substr(end.col);

    const auto first = lines_.begin() + static_cast<std::ptrdiff_t>(start.row);
    const auto last = lines_.begin() + static_cast<std::ptrdiff_t>(end.row) + 1;
    lines_.erase(first, last);
    lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(start.row),
                  std::make_move_iterator(pieces.begin()), std::make_move_iterator(pieces.end()));
    return edit;
}

}