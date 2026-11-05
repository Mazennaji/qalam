#include "motions/objects.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace qalam {
namespace {

int charClass(char c) {
    const auto u = static_cast<unsigned char>(c);
    if (c == ' ' || c == '\t') return 0;
    if (u >= 128 || std::isalnum(u) || c == '_') return 1;
    return 2;
}

bool isBlank(char c) {
    return c == ' ' || c == '\t';
}

bool blankLine(const std::string& line) {
    return line.find_first_not_of(" \t") == std::string::npos;
}

char charAt(const TextBuffer& buffer, Pos p) {
    const std::string& line = buffer.line(p.row);
    return p.col < line.size() ? line[p.col] : '\0';
}

std::optional<TextRange> wordObject(const TextBuffer& buffer, Pos from, bool around, int count) {
    const std::string& line = buffer.line(from.row);
    const std::size_t n = line.size();
    if (n == 0) return std::nullopt;

    const std::size_t col = std::min(from.col, n - 1);
    const int cls = charClass(line[col]);

    auto run = [&](std::size_t& end, int kind) {
        while (end < n && charClass(line[end]) == kind) ++end;
    };

    std::size_t s = col;
    while (s > 0 && charClass(line[s - 1]) == cls) --s;
    std::size_t e = col;
    run(e, cls);

    if (!around) {
        for (int i = 1; i < count && e < n; ++i) run(e, charClass(line[e]));
        return TextRange{{from.row, s}, {from.row, e}, false};
    }

    if (cls == 0) {
        if (e < n) run(e, charClass(line[e]));
    } else {
        const std::size_t before = e;
        run(e, 0);
        if (e == before) {
            while (s > 0 && charClass(line[s - 1]) == 0) --s;
        }
    }

    for (int i = 1; i < count && e < n; ++i) {
        const int kind = charClass(line[e]);
        run(e, kind);
        if (kind == 0) {
            if (e < n) run(e, charClass(line[e]));
        } else {
            run(e, 0);
        }
    }
    return TextRange{{from.row, s}, {from.row, e}, false};
}

std::optional<TextRange> quoteObject(const TextBuffer& buffer, Pos from, char quote,
                                     bool around) {
    const std::string& line = buffer.line(from.row);

    std::vector<std::size_t> marks;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\') {
            ++i;
            continue;
        }
        if (line[i] == quote) marks.push_back(i);
    }

    for (std::size_t k = 0; k + 1 < marks.size(); k += 2) {
        const std::size_t p = marks[k];
        const std::size_t q = marks[k + 1];
        if (q < from.col) continue;

        if (!around) return TextRange{{from.row, p + 1}, {from.row, q}, false};

        std::size_t s = p;
        std::size_t e = q + 1;
        std::size_t t = e;
        while (t < line.size() && isBlank(line[t])) ++t;
        if (t > e) {
            e = t;
        } else {
            while (s > 0 && isBlank(line[s - 1])) --s;
        }
        return TextRange{{from.row, s}, {from.row, e}, false};
    }
    return std::nullopt;
}

std::optional<Pos> findOpen(const TextBuffer& buffer, Pos p, char open, char close, int count) {
    int depth = 0;
    int need = count;
    bool first = true;

    while (true) {
        const char c = charAt(buffer, p);
        if (c == open) {
            if (depth == 0) {
                if (--need <= 0) return p;
            } else {
                --depth;
            }
        } else if (c == close && !first) {
            ++depth;
        }
        first = false;

        if (p.col > 0) {
            --p.col;
        } else {
            if (p.row == 0) return std::nullopt;
            --p.row;
            const std::size_t size = buffer.line(p.row).size();
            p.col = size == 0 ? 0 : size - 1;
        }
    }
}

std::optional<Pos> findClose(const TextBuffer& buffer, Pos p, char open, char close) {
    int depth = 0;

    while (true) {
        const std::size_t size = buffer.line(p.row).size();
        if (p.col + 1 < size) {
            ++p.col;
        } else {
            if (p.row + 1 >= buffer.lineCount()) return std::nullopt;
            ++p.row;
            p.col = 0;
        }

        const char c = charAt(buffer, p);
        if (c == open) {
            ++depth;
        } else if (c == close) {
            if (depth == 0) return p;
            --depth;
        }
    }
}

std::optional<TextRange> bracketObject(const TextBuffer& buffer, Pos from, char open, char close,
                                       bool around, int count) {
    const std::optional<Pos> o = findOpen(buffer, from, open, close, count);
    if (!o) return std::nullopt;
    const std::optional<Pos> c = findClose(buffer, *o, open, close);
    if (!c) return std::nullopt;

    if (around) return TextRange{*o, {c->row, c->col + 1}, false};

    const std::string& openLine = buffer.line(o->row);
    const std::string& closeLine = buffer.line(c->row);

    if (c->row > o->row + 1) {
        const bool openLast = o->col + 1 >= openLine.size();
        const bool closeFirst = closeLine.find_first_not_of(" \t") == c->col;
        if (openLast && closeFirst) {
            return TextRange{{o->row + 1, 0}, {c->row - 1, 0}, true};
        }
    }

    Pos start{o->row, o->col + 1};
    if (start.col >= openLine.size() && c->row > o->row) start = {o->row + 1, 0};
    return TextRange{start, *c, false};
}

std::optional<TextRange> paragraphObject(const TextBuffer& buffer, Pos from, bool around,
                                         int count) {
    const std::size_t total = buffer.lineCount();
    const bool state = blankLine(buffer.line(from.row));

    std::size_t s = from.row;
    std::size_t e = from.row;
    while (s > 0 && blankLine(buffer.line(s - 1)) == state) --s;
    while (e + 1 < total && blankLine(buffer.line(e + 1)) == state) ++e;

    auto extend = [&]() {
        if (e + 1 >= total) return false;
        const bool group = blankLine(buffer.line(e + 1));
        ++e;
        while (e + 1 < total && blankLine(buffer.line(e + 1)) == group) ++e;
        return true;
    };

    if (!around) {
        for (int i = 1; i < count; ++i) {
            if (!extend()) break;
        }
    } else {
        const bool extended = extend();
        if (!extended && !state) {
            while (s > 0 && blankLine(buffer.line(s - 1))) --s;
        }
        for (int i = 1; i < count; ++i) {
            if (!extend()) break;
            if (!extend()) break;
        }
    }
    return TextRange{{s, 0}, {e, 0}, true};
}

}

std::optional<TextRange> textObject(const TextBuffer& buffer, Pos from, char kind, bool around,
                                    int count) {
    switch (kind) {
        case 'w': return wordObject(buffer, from, around, count);
        case '"':
        case '\'':
        case '`': return quoteObject(buffer, from, kind, around);
        case '(':
        case ')':
        case 'b': return bracketObject(buffer, from, '(', ')', around, count);
        case '{':
        case '}':
        case 'B': return bracketObject(buffer, from, '{', '}', around, count);
        case '[':
        case ']': return bracketObject(buffer, from, '[', ']', around, count);
        case '<':
        case '>': return bracketObject(buffer, from, '<', '>', around, count);
        case 'p': return paragraphObject(buffer, from, around, count);
        default: return std::nullopt;
    }
}

}