#include "search/substitute.h"

#include <vector>

namespace qalam {

    SubstituteResult substituteLine(const std::string& line, const std::string& pattern,
                                    const std::string& replacement, bool global, CaseMode mode) {
        std::vector<Span> spans = findInLine(line, pattern, mode);
        if (spans.empty()) return {line, 0};
        if (!global) spans.resize(1);

        std::string out;
        std::size_t cursor = 0;
        for (const Span& span : spans) {
            out.append(line, cursor, span.first - cursor);
            out += replacement;
            cursor = span.second;
        }
        out.append(line, cursor, std::string::npos);
        return {out, spans.size()};
    }

}