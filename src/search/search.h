#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "buffer/pos.h"
#include "buffer/text_buffer.h"

namespace qalam {

    enum class CaseMode { Smart, Insensitive, Sensitive };

    using Span = std::pair<std::size_t, std::size_t>;

    struct SearchHit {
        Pos pos;
        bool wrapped = false;
    };

    std::vector<Span> findInLine(const std::string& line, const std::string& pattern,
                                 CaseMode mode = CaseMode::Smart);

    std::optional<SearchHit> searchForward(const TextBuffer& buffer, Pos from,
                                           const std::string& pattern);

    std::optional<SearchHit> searchBackward(const TextBuffer& buffer, Pos from,
                                            const std::string& pattern);

}