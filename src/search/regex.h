#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace qalam {

    constexpr std::size_t kNoPos = static_cast<std::size_t>(-1);

    struct Match {
        std::size_t begin = 0;
        std::size_t end = 0;
        std::array<std::pair<std::size_t, std::size_t>, 10> groups;

        Match() { groups.fill({kNoPos, kNoPos}); }
    };

    class Regex {
    public:
        static std::optional<Regex> compile(const std::string& pattern, bool ignoreCase,
                                            std::string& error);

        std::optional<Match> search(const std::string& text, std::size_t from) const;

    private:
        struct Impl;
        std::shared_ptr<const Impl> impl_;
    };

}