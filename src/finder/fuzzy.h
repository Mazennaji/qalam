#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace qalam {
    std::optional<int> fuzzyScore(const std::string &query, const std::string &candidate);

    std::vector<std::string> rankFuzzy(const std::string &query,
                                       const std::vector<std::string> &candidates,
                                       std::size_t limit);

    std::vector<std::string> listFiles(const std::string &root, std::size_t limit);
}
