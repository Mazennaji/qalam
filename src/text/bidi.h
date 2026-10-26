#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace qalam {
    struct VisualRun {
        std::size_t begin = 0;
        std::size_t end = 0;
        bool rtl = false;
    };

    std::vector<VisualRun> visualRuns(const std::string &line);
}
