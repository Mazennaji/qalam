#pragma once

#include <algorithm>
#include <cstddef>
#include <string>

#include "buffer/pos.h"

namespace qalam {

    struct Edit {
        Pos start;
        std::string removed;
        std::string inserted;
    };

    inline Pos endOf(Pos start, const std::string& text) {
        const auto newlines = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
        if (newlines == 0) return {start.row, start.col + text.size()};
        return {start.row + newlines, text.size() - text.rfind('\n') - 1};
    }

}