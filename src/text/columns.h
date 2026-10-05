#pragma once

#include <cstddef>
#include <string>

namespace qalam {

    constexpr int kTabStop = 4;

    int displayColumn(const std::string& line, std::size_t index);

    std::string expandTabs(const std::string& line);

}