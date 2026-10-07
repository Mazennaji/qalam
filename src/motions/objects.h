#pragma once

#include <optional>

#include "buffer/pos.h"
#include "buffer/text_buffer.h"

namespace qalam {

    struct TextRange {
        Pos start;
        Pos end;
        bool linewise = false;
    };

    std::optional<TextRange> textObject(const TextBuffer& buffer, Pos from, char kind, bool around,
                                        int count);

}