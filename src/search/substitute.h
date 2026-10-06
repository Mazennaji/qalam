#pragma once

#include <cstddef>
#include <string>

#include "search/search.h"

namespace qalam {

    struct SubstituteResult {
        std::string line;
        std::size_t count = 0;
    };

    SubstituteResult substituteLine(const std::string& line, const std::string& pattern,
                                    const std::string& replacement, bool global, CaseMode mode);

}