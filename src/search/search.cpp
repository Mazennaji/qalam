#include "search/search.h"

#include <algorithm>
#include <cctype>

#include "text/columns.h"

namespace qalam {
    namespace {
        bool hasUpper(const std::string &s) {
            for (std::size_t i = 0; i < s.size(); ++i) {
                if (s[i] == '\\') {
                    ++i;
                    continue;
                }
                if (std::isupper(static_cast<unsigned char>(s[i])) != 0) return true;
            }
            return false;
        }

        bool foldsCase(const std::string &pattern, CaseMode mode) {
            switch (mode) {
                case CaseMode::Insensitive: return true;
                case CaseMode::Sensitive: return false;
                case CaseMode::Smart: return !hasUpper(pattern);
            }
            return false;
        }

        const Regex *compiled(const std::string &pattern, bool fold, std::string *error) {
            static std::string lastPattern;
            static std::string lastError;
            static bool lastFold = false;
            static bool have = false;
            static std::optional<Regex> regex;

            if (!have || pattern != lastPattern || fold != lastFold) {
                lastError.clear();
                regex = Regex::compile(pattern, fold, lastError);
                lastPattern = pattern;
                lastFold = fold;
                have = true;
            }

            if (error != nullptr) *error = lastError;
            return regex ? &*regex : nullptr;
        }
    }

    bool validatePattern(const std::string &pattern, std::string &error) {
        if (pattern.empty()) return true;
        return compiled(pattern, false, &error) != nullptr;
    }

    std::vector<Match> matchesInLine(const std::string &line, const std::string &pattern,
                                     CaseMode mode) {
        std::vector<Match> found;
        if (pattern.empty()) return found;

        const Regex *regex = compiled(pattern, foldsCase(pattern, mode), nullptr);
        if (regex == nullptr) return found;

        std::size_t at = 0;
        while (at <= line.size()) {
            const std::optional<Match> m = regex->search(line, at);
            if (!m) break;
            found.push_back(*m);
            if (m->end > m->begin) {
                at = m->end;
            } else {
                at = m->begin + std::max<std::size_t>(1, charLength(line, m->begin));
            }
        }
        return found;
    }

    std::vector<Span> findInLine(const std::string &line, const std::string &pattern,
                                 CaseMode mode) {
        std::vector<Span> spans;
        for (const Match &m: matchesInLine(line, pattern, mode)) {
            spans.emplace_back(m.begin, m.end);
        }
        return spans;
    }

    std::optional<SearchHit> searchForward(const TextBuffer &buffer, Pos from,
                                           const std::string &pattern) {
        const std::size_t count = buffer.lineCount();
        for (std::size_t i = 0; i <= count; ++i) {
            const std::size_t row = (from.row + i) % count;
            for (const Span &span: findInLine(buffer.line(row), pattern)) {
                if (i == 0 && span.first <= from.col) continue;
                return SearchHit{{row, span.first}, from.row + i >= count};
            }
        }
        return std::nullopt;
    }

    std::optional<SearchHit> searchBackward(const TextBuffer &buffer, Pos from,
                                            const std::string &pattern) {
        const std::size_t count = buffer.lineCount();
        for (std::size_t i = 0; i <= count; ++i) {
            const std::size_t row = (from.row + count - (i % count)) % count;
            const std::vector<Span> spans = findInLine(buffer.line(row), pattern);
            for (auto it = spans.rbegin(); it != spans.rend(); ++it) {
                if (i == 0 && it->first >= from.col) continue;
                return SearchHit{{row, it->first}, i > from.row};
            }
        }
        return std::nullopt;
    }
}
