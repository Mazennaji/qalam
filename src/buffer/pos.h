#pragma once

#include <compare>
#include <cstddef>

namespace qalam {

    struct Pos {
        std::size_t row = 0;
        std::size_t col = 0;

        auto operator<=>(const Pos&) const = default;
    };

}