#include "text/columns.h"

#include <algorithm>

namespace qalam {
    namespace {
        struct Interval {
            char32_t first;
            char32_t last;
        };

        constexpr Interval kZeroWidth[] = {
            {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x05BF, 0x05BF}, {0x05C1, 0x05C2},
            {0x05C4, 0x05C5}, {0x05C7, 0x05C7}, {0x0610, 0x061A}, {0x064B, 0x065F}, {0x0670, 0x0670},
            {0x06D6, 0x06DC}, {0x06DF, 0x06E4}, {0x06E7, 0x06E8}, {0x06EA, 0x06ED}, {0x0E31, 0x0E31},
            {0x0E34, 0x0E3A}, {0x0E47, 0x0E4E}, {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF}, {0x200B, 0x200F},
            {0x202A, 0x202E}, {0x2060, 0x2064}, {0x20D0, 0x20FF}, {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F},
            {0xFEFF, 0xFEFF},
        };

        constexpr Interval kWide[] = {
            {0x1100, 0x115F}, {0x2E80, 0x303E}, {0x3041, 0x33FF}, {0x3400, 0x4DBF},
            {0x4E00, 0x9FFF}, {0xA000, 0xA4CF}, {0xAC00, 0xD7A3}, {0xF900, 0xFAFF},
            {0xFE30, 0xFE6F}, {0xFF00, 0xFF60}, {0xFFE0, 0xFFE6}, {0x1F300, 0x1F64F},
            {0x1F900, 0x1F9FF}, {0x20000, 0x3FFFD},
        };

        template<std::size_t N>
        bool contains(const Interval (&table)[N], char32_t cp) {
            for (const Interval &range: table) {
                if (cp >= range.first && cp <= range.last) return true;
            }
            return false;
        }

        bool isContinuation(char c) {
            return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
        }

        char32_t decode(const std::string &line, std::size_t index, std::size_t length) {
            const auto lead = static_cast<unsigned char>(line[index]);
            if (length == 1) return lead;

            char32_t cp = length == 2 ? (lead & 0x1Fu) : length == 3 ? (lead & 0x0Fu) : (lead & 0x07u);
            for (std::size_t k = 1; k < length; ++k) {
                cp = (cp << 6) | (static_cast<unsigned char>(line[index + k]) & 0x3Fu);
            }
            return cp;
        }

        std::size_t cellWidth(const std::string &line, std::size_t index, std::size_t length,
                              std::size_t column) {
            if (line[index] == '\t') {
                const auto stop = static_cast<std::size_t>(kTabStop);
                return stop - column % stop;
            }
            if (length == 1) return 1;

            const char32_t cp = decode(line, index, length);
            if (contains(kZeroWidth, cp)) return 0;
            if (contains(kWide, cp)) return 2;
            return 1;
        }
    }

    std::size_t charLength(const std::string &line, std::size_t index) {
        if (index >= line.size()) return 0;

        const auto lead = static_cast<unsigned char>(line[index]);
        std::size_t length = 1;
        if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
        }

        if (length == 1 || index + length > line.size()) return 1;
        for (std::size_t k = 1; k < length; ++k) {
            if (!isContinuation(line[index + k])) return 1;
        }
        return length;
    }

    std::size_t prevCharStart(const std::string &line, std::size_t index) {
        index = std::min(index, line.size());
        if (index == 0) return 0;

        std::size_t start = index - 1;
        while (start > 0 && isContinuation(line[start]) && index - start < 4) --start;
        if (start + charLength(line, start) == index) return start;
        return index - 1;
    }

    std::size_t snapToStart(const std::string &line, std::size_t index) {
        if (index >= line.size()) return index;

        std::size_t start = index;
        while (start > 0 && isContinuation(line[start]) && index - start < 3) --start;
        if (start + charLength(line, start) > index) return start;
        return index;
    }

    int displayColumn(const std::string &line, std::size_t index) {
        std::size_t column = 0;
        std::size_t i = 0;
        const std::size_t end = std::min(index, line.size());
        while (i < end) {
            const std::size_t length = charLength(line, i);
            column += cellWidth(line, i, length, column);
            i += length;
        }
        return static_cast<int>(column);
    }

    int displayWidth(const std::string &line) {
        return displayColumn(line, line.size());
    }

    std::size_t byteForColumn(const std::string &line, std::size_t column) {
        std::size_t i = 0;
        std::size_t current = 0;
        while (i < line.size()) {
            const std::size_t length = charLength(line, i);
            const std::size_t width = cellWidth(line, i, length, current);
            if (current + width > column) return i;
            current += width;
            i += length;
        }
        return line.size();
    }

    std::pair<std::size_t, std::size_t> byteSpan(const std::string &line, std::size_t left,
                                                 std::size_t right) {
        std::size_t from = line.size();
        std::size_t to = line.size();
        bool found = false;

        std::size_t i = 0;
        std::size_t column = 0;
        while (i < line.size()) {
            const std::size_t length = charLength(line, i);
            const std::size_t width = cellWidth(line, i, length, column);
            const std::size_t finish = column + width;

            if (width > 0 && finish > left && column <= right) {
                if (!found) {
                    from = i;
                    found = true;
                }
                to = i + length;
            } else if (found && width == 0) {
                to = i + length;
            }

            if (column > right) break;
            column = finish;
            i += length;
        }
        return {from, to};
    }

    std::string truncateToWidth(const std::string &text, std::size_t width) {
        std::size_t i = 0;
        std::size_t column = 0;
        while (i < text.size()) {
            const std::size_t length = charLength(text, i);
            const std::size_t cell = cellWidth(text, i, length, column);
            if (column + cell > width) break;
            column += cell;
            i += length;
        }
        return text.substr(0, i);
    }

    std::vector<std::string> displayCells(const std::string &line) {
        std::vector<std::string> cells;
        std::size_t i = 0;

        while (i < line.size()) {
            const std::size_t length = charLength(line, i);
            const auto lead = static_cast<unsigned char>(line[i]);

            if (lead == '\t') {
                const auto stop = static_cast<std::size_t>(kTabStop);
                cells.insert(cells.end(), stop - cells.size() % stop, " ");
            } else if (length == 1 && (lead < 0x20 || lead >= 0x7F)) {
                cells.emplace_back("?");
            } else {
                const std::size_t width = cellWidth(line, i, length, cells.size());
                const std::string glyph = line.substr(i, length);
                if (width == 0) {
                    std::size_t k = cells.size();
                    while (k > 0 && cells[k - 1].empty()) --k;
                    if (k > 0) cells[k - 1] += glyph;
                } else {
                    cells.push_back(glyph);
                    if (width == 2) cells.emplace_back();
                }
            }
            i += length;
        }
        return cells;
    }

    std::vector<std::size_t> cellSources(const std::string &line) {
        std::vector<std::size_t> sources;
        std::size_t i = 0;

        while (i < line.size()) {
            const std::size_t length = charLength(line, i);
            const auto lead = static_cast<unsigned char>(line[i]);

            if (lead == '\t') {
                const auto stop = static_cast<std::size_t>(kTabStop);
                sources.insert(sources.end(), stop - sources.size() % stop, i);
            } else if (length == 1 && (lead < 0x20 || lead >= 0x7F)) {
                sources.push_back(i);
            } else {
                const std::size_t width = cellWidth(line, i, length, sources.size());
                if (width > 0) {
                    sources.push_back(i);
                    if (width == 2) sources.push_back(i);
                }
            }
            i += length;
        }
        return sources;
    }

    int wrapHeight(const std::string &line, int width) {
        const int w = std::max(1, width);
        return std::max(1, (displayWidth(line) + w - 1) / w);
    }

    void popLastChar(std::string &text) {
        if (text.empty()) return;
        text.erase(prevCharStart(text, text.size()));
    }
}
