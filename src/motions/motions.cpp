#include "motions/motions.h"

#include <cctype>
#include <string>

namespace qalam {
namespace {

int charClass(char c) {
    const auto u = static_cast<unsigned char>(c);
    if (c == ' ' || c == '\t') return 0;
    if (u >= 128 || std::isalnum(u) || c == '_') return 1;
    return 2;
}

std::size_t lastColumn(const std::string& line) {
    return line.empty() ? 0 : line.size() - 1;
}

Pos nextWordStart(const TextBuffer& buffer, Pos p) {
    const std::string& first = buffer.line(p.row);
    if (p.col < first.size()) {
        const int cls = charClass(first[p.col]);
        if (cls != 0) {
            while (p.col < first.size() && charClass(first[p.col]) == cls) ++p.col;
        }
    }

    while (true) {
        const std::string& line = buffer.line(p.row);
        while (p.col < line.size() && charClass(line[p.col]) == 0) ++p.col;
        if (p.col < line.size()) return p;
        if (p.row + 1 >= buffer.lineCount()) return {p.row, lastColumn(line)};
        ++p.row;
        p.col = 0;
        if (buffer.line(p.row).empty()) return p;
    }
}

Pos prevWordStart(const TextBuffer& buffer, Pos p) {
    while (true) {
        if (p.col == 0) {
            if (p.row == 0) return p;
            --p.row;
            const std::string& previous = buffer.line(p.row);
            if (previous.empty()) return {p.row, 0};
            p.col = previous.size();
        }
        --p.col;
        if (charClass(buffer.line(p.row)[p.col]) != 0) break;
    }

    const std::string& line = buffer.line(p.row);
    const int cls = charClass(line[p.col]);
    while (p.col > 0 && charClass(line[p.col - 1]) == cls) --p.col;
    return p;
}

bool advance(const TextBuffer& buffer, Pos& p) {
    const std::string& line = buffer.line(p.row);
    if (p.col + 1 < line.size()) {
        ++p.col;
        return true;
    }
    if (p.row + 1 >= buffer.lineCount()) return false;
    ++p.row;
    p.col = 0;
    return true;
}

Pos nextWordEnd(const TextBuffer& buffer, Pos p) {
    if (!advance(buffer, p)) return p;

    while (true) {
        const std::string& line = buffer.line(p.row);
        if (!line.empty() && charClass(line[p.col]) != 0) break;
        if (!advance(buffer, p)) return p;
    }

    const std::string& line = buffer.line(p.row);
    const int cls = charClass(line[p.col]);
    while (p.col + 1 < line.size() && charClass(line[p.col + 1]) == cls) ++p.col;
    return p;
}

Pos nextParagraph(const TextBuffer& buffer, Pos p) {
    const std::size_t count = buffer.lineCount();
    std::size_t row = p.row;
    while (row < count && buffer.line(row).empty()) ++row;
    while (row < count && !buffer.line(row).empty()) ++row;
    if (row >= count) return {count - 1, lastColumn(buffer.line(count - 1))};
    return {row, 0};
}

Pos prevParagraph(const TextBuffer& buffer, Pos p) {
    std::size_t row = p.row;
    while (row > 0 && buffer.line(row).empty()) --row;
    while (row > 0 && !buffer.line(row).empty()) --row;
    return {row, 0};
}

}

Motion wordForward(const TextBuffer& buffer, Pos from, int count) {
    for (int i = 0; i < count; ++i) from = nextWordStart(buffer, from);
    return {from, false, false};
}

Motion wordBackward(const TextBuffer& buffer, Pos from, int count) {
    for (int i = 0; i < count; ++i) from = prevWordStart(buffer, from);
    return {from, false, false};
}

Motion wordEnd(const TextBuffer& buffer, Pos from, int count) {
    for (int i = 0; i < count; ++i) from = nextWordEnd(buffer, from);
    return {from, false, true};
}

Motion firstNonBlank(const TextBuffer& buffer, Pos from) {
    const std::string& line = buffer.line(from.row);
    const auto pos = line.find_first_not_of(" \t");
    return {{from.row, pos == std::string::npos ? line.size() : pos}, false, false};
}

Motion paragraphForward(const TextBuffer& buffer, Pos from, int count) {
    for (int i = 0; i < count; ++i) from = nextParagraph(buffer, from);
    return {from, false, false};
}

Motion paragraphBackward(const TextBuffer& buffer, Pos from, int count) {
    for (int i = 0; i < count; ++i) from = prevParagraph(buffer, from);
    return {from, false, false};
}

std::optional<Motion> findChar(const TextBuffer& buffer, Pos from, char ch, bool forward,
                               bool till, int count) {
    const std::string& line = buffer.line(from.row);
    std::size_t i = from.col;

    for (int n = 0; n < count; ++n) {
        if (forward) {
            i = line.find(ch, i + 1);
        } else {
            if (i == 0) return std::nullopt;
            i = line.rfind(ch, i - 1);
        }
        if (i == std::string::npos) return std::nullopt;
    }

    if (till) i = forward ? i - 1 : i + 1;
    return Motion{{from.row, i}, false, forward};
}

}