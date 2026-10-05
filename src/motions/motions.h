#pragma once

#include <cstddef>
#include <optional>

#include "buffer/text_buffer.h"

namespace qalam {

    struct Pos {
        std::size_t row = 0;
        std::size_t col = 0;
    };

    struct Motion {
        Pos target;
        bool linewise = false;
        bool inclusive = false;
    };

    Motion wordForward(const TextBuffer& buffer, Pos from, int count);
    Motion wordBackward(const TextBuffer& buffer, Pos from, int count);
    Motion wordEnd(const TextBuffer& buffer, Pos from, int count);
    Motion firstNonBlank(const TextBuffer& buffer, Pos from);
    Motion paragraphForward(const TextBuffer& buffer, Pos from, int count);
    Motion paragraphBackward(const TextBuffer& buffer, Pos from, int count);

    std::optional<Motion> findChar(const TextBuffer& buffer, Pos from, char ch, bool forward,
                                   bool till, int count);

}