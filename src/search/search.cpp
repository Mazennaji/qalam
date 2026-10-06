#include "search/search.h"

#include <cctype>

namespace qalam {
namespace {

std::string lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool hasUpper(const std::string& s) {
    for (char c : s) {
        if (std::isupper(static_cast<unsigned char>(c))) return true;
    }
    return false;
}

bool foldsCase(const std::string& pattern, CaseMode mode) {
    switch (mode) {
        case CaseMode::Insensitive: return true;
        case CaseMode::Sensitive: return false;
        case CaseMode::Smart: return !hasUpper(pattern);
    }
    return false;
}

}

std::vector<Span> findInLine(const std::string& line, const std::string& pattern,
                             CaseMode mode) {
    std::vector<Span> spans;
    if (pattern.empty()) return spans;

    const bool fold = foldsCase(pattern, mode);
    const std::string hay = fold ? lower(line) : line;
    const std::string needle = fold ? lower(pattern) : pattern;

    std::size_t at = 0;
    while ((at = hay.find(needle, at)) != std::string::npos) {
        spans.emplace_back(at, at + needle.size());
        at += needle.size();
    }
    return spans;
}

std::optional<SearchHit> searchForward(const TextBuffer& buffer, Pos from,
                                       const std::string& pattern) {
    const std::size_t count = buffer.lineCount();
    for (std::size_t i = 0; i <= count; ++i) {
        const std::size_t row = (from.row + i) % count;
        for (const Span& span : findInLine(buffer.line(row), pattern)) {
            if (i == 0 && span.first <= from.col) continue;
            return SearchHit{{row, span.first}, from.row + i >= count};
        }
    }
    return std::nullopt;
}

std::optional<SearchHit> searchBackward(const TextBuffer& buffer, Pos from,
                                        const std::string& pattern) {
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