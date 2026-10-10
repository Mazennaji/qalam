#include "motions/motions.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "text/columns.h"

namespace qalam {
namespace {

int charClass(char c) {
    const auto u = static_cast<unsigned char>(c);
    if (c == ' ' || c == '\t') return 0;
    if (u >= 128 || std::isalnum(u) || c == '_') return 1;
    return 2;
}

std::size_t lastColumn(const std::string& line) {
    return prevCharStart(line, line.size());
}

Pos nextWordStart(const TextBuffer& buffer, Pos p, bool& hitEnd) {
    hitEnd = false;

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
        if (p.row + 1 >= buffer.lineCount()) {
            hitEnd = true;
            return {p.row, lastColumn(line)};
        }
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
    const std::size_t next = p.col + charLength(line, p.col);
    if (next < line.size()) {
        p.col = next;
        return true;
    }
    if (p.row + 1 >= buffer.lineCount()) return false;
    ++p.row;
    p.col = 0;
    return true;
}

void extendWithinWord(const std::string& line, std::size_t& col, int cls) {
    while (true) {
        const std::size_t next = col + charLength(line, col);
        if (next >= line.size() || charClass(line[next]) != cls) return;
        col = next;
    }
}

Pos nextWordEnd(const TextBuffer& buffer, Pos p) {
    if (!advance(buffer, p)) return p;

    while (true) {
        const std::string& line = buffer.line(p.row);
        if (!line.empty() && charClass(line[p.col]) != 0) break;
        if (!advance(buffer, p)) return p;
    }

    const std::string& line = buffer.line(p.row);
    extendWithinWord(line, p.col, charClass(line[p.col]));
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
    for (int i = 0; i < count; ++i) {
        bool hitEnd = false;
        from = nextWordStart(buffer, from, hitEnd);
    }
    return {from, false, false};
}

Motion wordForwardForOperator(const TextBuffer& buffer, Pos from, int count) {
    for (int i = 0; i < count; ++i) {
        bool hitEnd = false;
        const Pos next = nextWordStart(buffer, from, hitEnd);
        const bool last = i + 1 == count;
        if (last && (hitEnd || next.row > from.row)) {
            const std::size_t row = hitEnd ? next.row : from.row;
            return {{row, buffer.line(row).size()}, false, false};
        }
        from = next;
    }
    return {from, false, false};
}

Motion changeWord(const TextBuffer& buffer, Pos from, int count) {
    const std::string& line = buffer.line(from.row);
    if (from.col >= line.size() || charClass(line[from.col]) == 0) {
        return wordForwardForOperator(buffer, from, count);
    }

    Pos p = from;
    extendWithinWord(line, p.col, charClass(line[from.col]));
    for (int i = 1; i < count; ++i) p = nextWordEnd(buffer, p);
    return {p, false, true};
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

std::optional<Motion> charLeft(const TextBuffer& buffer, Pos from, int count) {
    if (from.col == 0) return std::nullopt;

    const std::string& line = buffer.line(from.row);
    std::size_t col = from.col;
    for (int i = 0; i < count && col > 0; ++i) col = prevCharStart(line, col);
    return Motion{{from.row, col}, false, false};
}

std::optional<Motion> charRight(const TextBuffer& buffer, Pos from, int count) {
    const std::string& line = buffer.line(from.row);
    if (from.col >= line.size()) return std::nullopt;

    std::size_t col = from.col;
    for (int i = 0; i < count && col < line.size(); ++i) col += charLength(line, col);
    return Motion{{from.row, col}, false, false};
}

std::optional<Motion> lineUp(Pos from, int count) {
    if (from.row == 0) return std::nullopt;
    const auto n = static_cast<std::size_t>(count);
    return Motion{{from.row > n ? from.row - n : 0, from.col}, true, false};
}

std::optional<Motion> lineDown(const TextBuffer& buffer, Pos from, int count) {
    if (from.row + 1 >= buffer.lineCount()) return std::nullopt;
    const auto n = static_cast<std::size_t>(count);
    return Motion{{std::min(from.row + n, buffer.lineCount() - 1), from.col}, true, false};
}

Motion lineStart(Pos from) {
    return {{from.row, 0}, false, false};
}

Motion lineEnd(const TextBuffer& buffer, Pos from) {
    return {{from.row, lastColumn(buffer.line(from.row))}, false, true};
}

Motion gotoLine(const TextBuffer& buffer, std::size_t row) {
    return {{std::min(row, buffer.lineCount() - 1), 0}, true, false};
}

std::optional<Motion> findChar(const TextBuffer& buffer, Pos from, const std::string& target,
                               bool forward, bool till, int count) {
    if (target.empty()) return std::nullopt;

    const std::string& line = buffer.line(from.row);
    std::size_t i = from.col;

    for (int n = 0; n < count; ++n) {
        if (forward) {
            i = line.find(target, i + 1);
        } else {
            if (i == 0) return std::nullopt;
            i = line.rfind(target, i - 1);
        }
        if (i == std::string::npos) return std::nullopt;
    }

    if (till) i = forward ? prevCharStart(line, i) : i + target.size();
    return Motion{{from.row, i}, false, forward};
}

std::optional<Motion> matchBracket(const TextBuffer& buffer, Pos from) {
    static const std::string opens = "([{";
    static const std::string closes = ")]}";

    const std::string& line = buffer.line(from.row);
    std::size_t i = from.col;
    while (i < line.size() && opens.find(line[i]) == std::string::npos &&
           closes.find(line[i]) == std::string::npos) {
        ++i;
    }
    if (i >= line.size()) return std::nullopt;

    std::size_t k = opens.find(line[i]);
    const bool forward = k != std::string::npos;
    if (!forward) k = closes.find(line[i]);
    const char open = opens[k];
    const char close = closes[k];
    int depth = 0;

    if (forward) {
        for (std::size_t r = from.row; r < buffer.lineCount(); ++r) {
            const std::string& l = buffer.line(r);
            for (std::size_t j = r == from.row ? i + 1 : 0; j < l.size(); ++j) {
                if (l[j] == open) {
                    ++depth;
                } else if (l[j] == close) {
                    if (depth == 0) return Motion{{r, j}, false, true};
                    --depth;
                }
            }
        }
    } else {
        for (std::size_t r = from.row + 1; r-- > 0;) {
            const std::string& l = buffer.line(r);
            std::size_t j = r == from.row ? i : l.size();
            while (j-- > 0) {
                if (l[j] == close) {
                    ++depth;
                } else if (l[j] == open) {
                    if (depth == 0) return Motion{{r, j}, false, true};
                    --depth;
                }
            }
        }
    }
    return std::nullopt;
}

}