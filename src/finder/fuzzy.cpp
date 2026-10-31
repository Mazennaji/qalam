#include "finder/fuzzy.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <filesystem>
#include <utility>

namespace qalam {
    namespace {
        constexpr int kNone = INT_MIN / 4;

        bool isSep(char c) {
            return c == '/' || c == '_' || c == '-' || c == '.' || c == ' ';
        }

        int boundaryBonus(const std::string &c, std::size_t j) {
            if (j == 0) return 14;
            const char p = c[j - 1];
            if (p == '/') return 18;
            if (isSep(p)) return 12;
            if (std::islower(static_cast<unsigned char>(p)) != 0 &&
                std::isupper(static_cast<unsigned char>(c[j])) != 0) {
                return 8;
            }
            return 0;
        }

        bool skipDir(const std::string &name) {
            if (!name.empty() && name[0] == '.') return true;
            if (name == "node_modules" || name == "target") return true;
            return name.compare(0, 5, "build") == 0;
        }
    }

    std::optional<int> fuzzyScore(const std::string &query, const std::string &cand) {
        if (query.empty()) return 0;
        if (query.size() > cand.size()) return std::nullopt;

        const bool sensitive = std::any_of(query.begin(), query.end(), [](char c) {
            return std::isupper(static_cast<unsigned char>(c)) != 0;
        });
        auto eq = [&](char a, char b) {
            if (sensitive) return a == b;
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        };

        std::size_t qi = 0;
        for (char ch: cand) {
            if (qi < query.size() && eq(query[qi], ch)) ++qi;
        }
        if (qi < query.size()) return std::nullopt;

        const std::size_t n = cand.size();
        std::vector<int> prev(n, kNone);
        std::vector<int> cur(n, kNone);

        for (std::size_t j = 0; j < n; ++j) {
            if (eq(query[0], cand[j])) {
                prev[j] = 16 + boundaryBonus(cand, j) - static_cast<int>(std::min<std::size_t>(j, 5));
            }
        }

        for (std::size_t i = 1; i < query.size(); ++i) {
            std::fill(cur.begin(), cur.end(), kNone);
            int run = kNone;
            for (std::size_t j = 0; j < n; ++j) {
                if (j >= 2 && prev[j - 2] != kNone) {
                    run = std::max(run, prev[j - 2] + static_cast<int>(j - 2));
                }
                if (!eq(query[i], cand[j])) continue;

                int best = kNone;
                if (run != kNone) best = run - static_cast<int>(j) + 1;
                if (j >= 1 && prev[j - 1] != kNone) best = std::max(best, prev[j - 1] + 15);
                if (best == kNone) continue;

                cur[j] = best + 16 + boundaryBonus(cand, j);
            }
            std::swap(prev, cur);
        }

        const int best = *std::max_element(prev.begin(), prev.end());
        if (best == kNone) return std::nullopt;
        return best - static_cast<int>(n) / 8;
    }

    std::vector<std::size_t> fuzzyPositions(const std::string& query, const std::string& cand) {
        std::vector<std::size_t> out;
        if (query.empty()) return out;

        const bool sensitive = std::any_of(query.begin(), query.end(), [](char c) {
            return std::isupper(static_cast<unsigned char>(c)) != 0;
        });
        auto eq = [&](char a, char b) {
            if (sensitive) return a == b;
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        };

        std::size_t qi = 0;
        for (std::size_t j = 0; j < cand.size() && qi < query.size(); ++j) {
            if (eq(query[qi], cand[j])) {
                out.push_back(j);
                ++qi;
            }
        }
        if (qi < query.size()) out.clear();
        return out;
    }

    std::vector<std::string> rankFuzzy(const std::string &query,
                                       const std::vector<std::string> &candidates,
                                       std::size_t limit) {
        std::vector<std::pair<int, std::size_t> > scored;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (const auto s = fuzzyScore(query, candidates[i])) scored.emplace_back(*s, i);
        }

        std::sort(scored.begin(), scored.end(), [&](const auto &a, const auto &b) {
            if (a.first != b.first) return a.first > b.first;
            const std::string &x = candidates[a.second];
            const std::string &y = candidates[b.second];
            if (x.size() != y.size()) return x.size() < y.size();
            return x < y;
        });

        std::vector<std::string> out;
        for (std::size_t i = 0; i < scored.size() && i < limit; ++i) {
            out.push_back(candidates[scored[i].second]);
        }
        return out;
    }

    std::vector<std::string> listFiles(const std::string &root, std::size_t limit) {
        namespace fs = std::filesystem;

        std::vector<std::string> out;
        std::error_code ec;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        if (ec) return out;

        const fs::path base(root);
        for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;

            const std::string name = it->path().filename().string();
            std::error_code probe;
            if (it->is_directory(probe)) {
                if (skipDir(name)) it.disable_recursion_pending();
                continue;
            }
            if (!name.empty() && name[0] == '.') continue;
            if (!it->is_regular_file(probe)) continue;

            out.push_back(it->path().lexically_relative(base).string());
            if (out.size() >= limit) break;
        }
        return out;
    }
}
