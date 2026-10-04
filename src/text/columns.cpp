#include "text/columns.h"

#include <algorithm>

namespace qalam {

    int displayColumn(const std::string& line, std::size_t index) {
        int column = 0;
        const std::size_t end = std::min(index, line.size());
        for (std::size_t i = 0; i < end; ++i) {
            if (line[i] == '\t') {
                column += kTabStop - column % kTabStop;
            } else {
                ++column;
            }
        }
        return column;
    }

    std::string expandTabs(const std::string& line) {
        std::string out;
        out.reserve(line.size());
        for (char c : line) {
            if (c == '\t') {
                const int spaces = kTabStop - static_cast<int>(out.size()) % kTabStop;
                out.append(static_cast<std::size_t>(spaces), ' ');
            } else {
                out += c;
            }
        }
        return out;
    }

}