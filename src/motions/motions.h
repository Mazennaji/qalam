#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "buffer/pos.h"
#include "buffer/text_buffer.h"

namespace qalam {

    struct Motion {
        Pos target;
        bool linewise = false;
        bool inclusive = false;
    };

    Motion wordForward(const TextBuffer& buffer, Pos from, int count);
    Motion wordForwardForOperator(const TextBuffer& buffer, Pos from, int count);
    Motion changeWord(const TextBuffer& buffer, Pos from, int count);
    Motion wordBackward(const TextBuffer& buffer, Pos from, int count);
    Motion wordEnd(const TextBuffer& buffer, Pos from, int count);
    Motion firstNonBlank(const TextBuffer& buffer, Pos from);
    Motion paragraphForward(const TextBuffer& buffer, Pos from, int count);
    Motion paragraphBackward(const TextBuffer& buffer, Pos from, int count);

    std::optional<Motion> charLeft(const TextBuffer& buffer, Pos from, int count);
    std::optional<Motion> charRight(const TextBuffer& buffer, Pos from, int count);
    std::optional<Motion> lineUp(Pos from, int count);
    std::optional<Motion> lineDown(const TextBuffer& buffer, Pos from, int count);
    Motion lineStart(Pos from);
    Motion lineEnd(const TextBuffer& buffer, Pos from);
    Motion gotoLine(const TextBuffer& buffer, std::size_t row);

    std::optional<Motion> findChar(const TextBuffer& buffer, Pos from, const std::string& target,
                                   bool forward, bool till, int count);

}