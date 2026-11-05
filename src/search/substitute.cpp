#include "search/substitute.h"

#include <vector>

namespace qalam {
    namespace {
        void appendGroup(std::string &out, const std::string &line, const Match &m, std::size_t index) {
            const auto &range = m.groups[index];
            if (range.first == kNoPos) return;
            out.append(line, range.first, range.second - range.first);
        }

        void expand(std::string &out, const std::string &replacement, const std::string &line,
                    const Match &m) {
            for (std::size_t i = 0; i < replacement.size(); ++i) {
                const char c = replacement[i];

                if (c == '&') {
                    appendGroup(out, line, m, 0);
                    continue;
                }

                if (c != '\\' || i + 1 >= replacement.size()) {
                    out += c;
                    continue;
                }

                const char next = replacement[++i];
                if (next >= '0' && next <= '9') {
                    appendGroup(out, line, m, static_cast<std::size_t>(next - '0'));
                } else if (next == 't') {
                    out += '\t';
                } else {
                    out += next;
                }
            }
        }
    }

    SubstituteResult substituteLine(const std::string &line, const std::string &pattern,
                                    const std::string &replacement, bool global, CaseMode mode) {
        std::vector<Match> matches = matchesInLine(line, pattern, mode);
        if (matches.empty()) return {line, 0};
        if (!global) matches.resize(1);

        std::string out;
        std::size_t cursor = 0;
        for (const Match &m: matches) {
            out.append(line, cursor, m.begin - cursor);
            expand(out, replacement, line, m);
            cursor = m.end;
        }
        out.append(line, cursor, std::string::npos);
        return {out, matches.size()};
    }
}
