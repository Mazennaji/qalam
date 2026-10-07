#include "syntax/syntax.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

namespace qalam {
namespace {

constexpr unsigned char kCode = 0;
constexpr unsigned char kBlock = 1;
constexpr unsigned char kRaw = 2;

constexpr std::array<int, kKindCount> kDefaults = {0, 176, 80, 114, 244, 215, 75};

const std::unordered_set<std::string>& keywords() {
    static const std::unordered_set<std::string> set = {
        "alignas",   "alignof",     "asm",         "auto",          "break",
        "case",      "catch",       "class",       "concept",       "const",
        "consteval", "constexpr",   "constinit",   "const_cast",    "continue",
        "co_await",  "co_return",   "co_yield",    "decltype",      "default",
        "delete",    "do",          "dynamic_cast", "else",         "enum",
        "explicit",  "export",      "extern",      "false",         "final",
        "for",       "friend",      "goto",        "if",            "inline",
        "mutable",   "namespace",   "new",         "noexcept",      "nullptr",
        "operator",  "override",    "private",     "protected",     "public",
        "register",  "reinterpret_cast", "requires", "return",      "sizeof",
        "static",    "static_assert", "static_cast", "struct",      "switch",
        "template",  "this",        "thread_local", "throw",        "true",
        "try",       "typedef",     "typeid",      "typename",      "union",
        "using",     "virtual",     "volatile",    "while",         "NULL",
    };
    return set;
}

const std::unordered_set<std::string>& types() {
    static const std::unordered_set<std::string> set = {
        "bool",     "char",     "char8_t",  "char16_t", "char32_t", "double",   "float",
        "int",      "long",     "short",    "signed",   "unsigned", "void",     "wchar_t",
        "size_t",   "ssize_t",  "ptrdiff_t", "intptr_t", "uintptr_t", "int8_t",  "uint8_t",
        "int16_t",  "uint16_t", "int32_t",  "uint32_t", "int64_t",  "uint64_t",
    };
    return set;
}

bool identChar(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '_' || u >= 128;
}

bool isBlank(char c) {
    return c == ' ' || c == '\t';
}

void fill(std::vector<Kind>* out, std::size_t from, std::size_t to, Kind kind) {
    if (out == nullptr) return;
    for (std::size_t i = from; i < to && i < out->size(); ++i) (*out)[i] = kind;
}

bool rawStart(const std::string& line, std::size_t quote, std::size_t& prefixStart,
              std::string& delim, std::size_t& open) {
    if (quote == 0 || line[quote - 1] != 'R') return false;

    std::size_t s = quote - 1;
    while (s > 0 && identChar(line[s - 1])) --s;
    const std::string prefix = line.substr(s, quote - s);
    if (prefix != "R" && prefix != "LR" && prefix != "uR" && prefix != "UR" && prefix != "u8R") {
        return false;
    }

    const std::size_t paren = line.find('(', quote + 1);
    if (paren == std::string::npos || paren - quote - 1 > 16) return false;

    const std::string candidate = line.substr(quote + 1, paren - quote - 1);
    if (candidate.find_first_of(" \\\"") != std::string::npos) return false;

    prefixStart = s;
    delim = candidate;
    open = paren;
    return true;
}

Syntax::State highlightLine(const std::string& line, Syntax::State state,
                            std::vector<Kind>* out) {
    const std::size_t n = line.size();
    if (out != nullptr) out->assign(n, Kind::Normal);
    std::size_t i = 0;

    if (state.mode == kBlock) {
        const std::size_t end = line.find("*/");
        if (end == std::string::npos) {
            fill(out, 0, n, Kind::Comment);
            return state;
        }
        fill(out, 0, end + 2, Kind::Comment);
        i = end + 2;
        state.mode = kCode;
    } else if (state.mode == kRaw) {
        const std::string closing = ")" + state.delim + "\"";
        const std::size_t end = line.find(closing);
        if (end == std::string::npos) {
            fill(out, 0, n, Kind::String);
            return state;
        }
        fill(out, 0, end + closing.size(), Kind::String);
        i = end + closing.size();
        state.mode = kCode;
        state.delim.clear();
    }

    if (i == 0) {
        const std::size_t j = line.find_first_not_of(" \t");
        if (j != std::string::npos && line[j] == '#') {
            std::size_t k = j + 1;
            while (k < n && isBlank(line[k])) ++k;
            std::size_t e = k;
            while (e < n && std::isalpha(static_cast<unsigned char>(line[e])) != 0) ++e;
            fill(out, j, e, Kind::Preproc);
            i = e;

            if (line.compare(k, e - k, "include") == 0) {
                std::size_t a = i;
                while (a < n && isBlank(line[a])) ++a;
                if (a < n && line[a] == '<') {
                    const std::size_t close = line.find('>', a);
                    const std::size_t stop = close == std::string::npos ? n : close + 1;
                    fill(out, a, stop, Kind::String);
                    i = stop;
                }
            }
        }
    }

    while (i < n) {
        const char c = line[i];
        const char next = i + 1 < n ? line[i + 1] : '\0';

        if (c == '/' && next == '/') {
            fill(out, i, n, Kind::Comment);
            break;
        }

        if (c == '/' && next == '*') {
            const std::size_t end = line.find("*/", i + 2);
            if (end == std::string::npos) {
                fill(out, i, n, Kind::Comment);
                state.mode = kBlock;
                return state;
            }
            fill(out, i, end + 2, Kind::Comment);
            i = end + 2;
            continue;
        }

        if (c == '"' || c == '\'') {
            std::size_t prefixStart = 0;
            std::size_t open = 0;
            std::string delim;
            if (c == '"' && rawStart(line, i, prefixStart, delim, open)) {
                const std::string closing = ")" + delim + "\"";
                const std::size_t end = line.find(closing, open + 1);
                if (end == std::string::npos) {
                    fill(out, prefixStart, n, Kind::String);
                    state.mode = kRaw;
                    state.delim = delim;
                    return state;
                }
                fill(out, prefixStart, end + closing.size(), Kind::String);
                i = end + closing.size();
                continue;
            }

            std::size_t j = i + 1;
            while (j < n && line[j] != c) {
                if (line[j] == '\\') ++j;
                ++j;
            }
            const std::size_t stop = std::min(j + 1, n);
            fill(out, i, stop, Kind::String);
            i = stop;
            continue;
        }

        const bool digit = std::isdigit(static_cast<unsigned char>(c)) != 0;
        const bool dotDigit =
            c == '.' && std::isdigit(static_cast<unsigned char>(next)) != 0;
        if (digit || dotDigit) {
            std::size_t j = i;
            while (j < n) {
                const char d = line[j];
                const bool quoteSep =
                    d == '\'' && j + 1 < n && std::isalnum(static_cast<unsigned char>(line[j + 1]));
                const bool sign = (d == '+' || d == '-') && j > i &&
                                  (line[j - 1] == 'e' || line[j - 1] == 'E' ||
                                   line[j - 1] == 'p' || line[j - 1] == 'P');
                if (std::isalnum(static_cast<unsigned char>(d)) == 0 && d != '.' && d != '_' &&
                    !quoteSep && !sign) {
                    break;
                }
                ++j;
            }
            fill(out, i, j, Kind::Number);
            i = j;
            continue;
        }

        const bool start = std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_' ||
                           static_cast<unsigned char>(c) >= 128;
        if (start) {
            std::size_t j = i;
            while (j < n && identChar(line[j])) ++j;
            const std::string word = line.substr(i, j - i);
            if (keywords().count(word) != 0) {
                fill(out, i, j, Kind::Keyword);
            } else if (types().count(word) != 0) {
                fill(out, i, j, Kind::Type);
            }
            i = j;
            continue;
        }

        ++i;
    }

    return state;
}

int kindIndex(const std::string& name) {
    if (name == "keyword") return 1;
    if (name == "type") return 2;
    if (name == "string") return 3;
    if (name == "comment") return 4;
    if (name == "number") return 5;
    if (name == "preproc") return 6;
    return -1;
}

}

Syntax::Syntax() : colors_(kDefaults) {}

void Syntax::setFile(const std::string& path) {
    cpp_ = false;
    const std::size_t dot = path.rfind('.');
    const std::size_t slash = path.find_last_of('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        std::string ext = path.substr(dot + 1);
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        static const std::unordered_set<std::string> known = {
            "c", "h", "cc", "cpp", "cxx", "hh", "hpp", "hxx", "ipp", "inl", "tpp",
        };
        cpp_ = known.count(ext) != 0;
    }
    invalidate(0);
}

void Syntax::setEnabled(bool enabled) {
    enabled_ = enabled;
}

bool Syntax::enabled() const {
    return enabled_;
}

bool Syntax::active() const {
    return enabled_ && cpp_;
}

bool Syntax::setColor(const std::string& name, int code) {
    const int index = kindIndex(name);
    if (index < 0 || code < 0 || code > 255) return false;
    colors_[static_cast<std::size_t>(index)] = code;
    return true;
}

void Syntax::resetColors() {
    colors_ = kDefaults;
}

void Syntax::invalidate(std::size_t row) {
    if (starts_.size() > row + 1) starts_.resize(row + 1);
}

const std::vector<Kind>& Syntax::kinds(const TextBuffer& buffer, std::size_t row) const {
    if (row >= buffer.lineCount()) {
        scratch_.clear();
        return scratch_;
    }

    if (starts_.empty()) starts_.emplace_back();
    while (starts_.size() <= row) {
        const std::size_t r = starts_.size() - 1;
        State next = highlightLine(buffer.line(r), starts_[r], nullptr);
        starts_.push_back(std::move(next));
    }

    highlightLine(buffer.line(row), starts_[row], &scratch_);
    return scratch_;
}

std::string Syntax::sgr(Kind kind) const {
    const auto index = static_cast<std::size_t>(kind);
    if (index == 0 || index >= kKindCount) return "";
    return "\x1b[38;5;" + std::to_string(colors_[index]) + "m";
}

}