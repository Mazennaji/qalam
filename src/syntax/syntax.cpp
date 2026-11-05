#include "syntax/syntax.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

#include "text/columns.h"

namespace qalam {
    struct Language {
        std::vector<std::string> names;
        std::vector<std::string> exts;
        std::vector<std::string> shebangs;
        std::unordered_set<std::string> keywords;
        std::unordered_set<std::string> types;
        std::vector<std::string> lineComments;
        std::string blockOpen;
        std::string blockClose;
        std::vector<std::pair<std::string, std::string> > longStrings;
        std::string quotes;
        bool preproc = false;
        bool rawStrings = false;
        bool numbers = true;
        bool decorators = false;
        bool attributes = false;
        bool variables = false;
        bool charLiterals = false;
        bool hashAfterSpace = false;
        bool markdown = false;
    };

    namespace {
        constexpr unsigned char kCode = 0;
        constexpr unsigned char kBlock = 1;
        constexpr unsigned char kString = 2;

        constexpr std::array<int, kKindCount> kDefaults = {0, 176, 80, 114, 244, 215, 75};

        std::unordered_set<std::string> words(const char *list) {
            std::unordered_set<std::string> set;
            std::istringstream in(list);
            std::string w;
            while (in >> w) set.insert(w);
            return set;
        }

        std::vector<std::string> split(const char *list) {
            std::vector<std::string> out;
            std::istringstream in(list);
            std::string w;
            while (in >> w) out.push_back(w);
            return out;
        }

        const std::vector<Language> &languages() {
            static const std::vector<Language> table = [] {
                std::vector<Language> t;
                Language l;

                l = Language{};
                l.names = split("cpp c c++");
                l.exts = split("c h cc cpp cxx hh hpp hxx ipp inl tpp");
                l.keywords = words(
                    "alignas alignof asm auto break case catch class concept const consteval constexpr "
                    "constinit const_cast continue co_await co_return co_yield decltype default delete "
                    "do dynamic_cast else enum explicit export extern false final for friend goto if "
                    "inline mutable namespace new noexcept nullptr operator override private protected "
                    "public register reinterpret_cast requires return sizeof static static_assert "
                    "static_cast struct switch template this thread_local throw true try typedef typeid "
                    "typename union using virtual volatile while NULL");
                l.types = words(
                    "bool char char8_t char16_t char32_t double float int long short signed unsigned "
                    "void wchar_t size_t ssize_t ptrdiff_t intptr_t uintptr_t int8_t uint8_t int16_t "
                    "uint16_t int32_t uint32_t int64_t uint64_t");
                l.lineComments = {"//"};
                l.blockOpen = "/*";
                l.blockClose = "*/";
                l.quotes = "\"'";
                l.preproc = true;
                l.rawStrings = true;
                t.push_back(l);

                l = Language{};
                l.names = split("python py");
                l.exts = split("py pyw");
                l.shebangs = split("python");
                l.keywords = words(
                    "False None True and as assert async await break class continue def del elif else "
                    "except finally for from global if import in is lambda nonlocal not or pass raise "
                    "return try while with yield match case");
                l.types = words("int float str bool list dict set tuple bytes object complex frozenset "
                    "bytearray");
                l.lineComments = {"#"};
                l.longStrings = {{"\"\"\"", "\"\"\""}, {"'''", "'''"}};
                l.quotes = "\"'";
                l.decorators = true;
                t.push_back(l);

                l = Language{};
                l.names = split("sh bash zsh shell");
                l.exts = split("sh bash zsh");
                l.shebangs = split("sh bash zsh dash ksh");
                l.keywords = words(
                    "if then else elif fi for while until do done case esac in function select time "
                    "return exit break continue local export readonly declare unset shift source alias "
                    "eval exec set trap");
                l.types = words("echo printf read test cd pwd true false");
                l.lineComments = {"#"};
                l.quotes = "\"'";
                l.variables = true;
                l.numbers = false;
                l.hashAfterSpace = true;
                t.push_back(l);

                l = Language{};
                l.names = split("javascript js typescript ts");
                l.exts = split("js mjs cjs jsx ts tsx");
                l.shebangs = split("node deno bun");
                l.keywords = words(
                    "break case catch class const continue debugger default delete do else export "
                    "extends finally for function if import in instanceof let new of return super "
                    "switch this throw try typeof var void while with yield async await static get set "
                    "null undefined true false interface type enum implements namespace declare "
                    "readonly abstract as from public private protected");
                l.types = words("string number boolean any unknown never object symbol bigint Array "
                    "Promise Map Set Date Error");
                l.lineComments = {"//"};
                l.blockOpen = "/*";
                l.blockClose = "*/";
                l.longStrings = {{"`", "`"}};
                l.quotes = "\"'";
                t.push_back(l);

                l = Language{};
                l.names = split("rust rs");
                l.exts = split("rs");
                l.keywords = words(
                    "as async await break const continue crate dyn else enum extern false fn for if impl "
                    "in let loop match mod move mut pub ref return self Self static struct super trait "
                    "true type unsafe use where while");
                l.types = words(
                    "i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str String "
                    "Vec Option Result Box");
                l.lineComments = {"//"};
                l.blockOpen = "/*";
                l.blockClose = "*/";
                l.quotes = "\"'";
                l.attributes = true;
                l.charLiterals = true;
                t.push_back(l);

                l = Language{};
                l.names = split("go golang");
                l.exts = split("go");
                l.keywords = words(
                    "break case chan const continue default defer else fallthrough for func go goto if "
                    "import interface map package range return select struct switch type var nil true "
                    "false iota");
                l.types = words(
                    "bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 "
                    "rune string uint uint8 uint16 uint32 uint64 uintptr any");
                l.lineComments = {"//"};
                l.blockOpen = "/*";
                l.blockClose = "*/";
                l.longStrings = {{"`", "`"}};
                l.quotes = "\"'";
                t.push_back(l);

                l = Language{};
                l.names = split("json");
                l.exts = split("json jsonc");
                l.keywords = words("true false null");
                l.lineComments = {"//"};
                l.blockOpen = "/*";
                l.blockClose = "*/";
                l.quotes = "\"";
                t.push_back(l);

                l = Language{};
                l.names = split("lua");
                l.exts = split("lua");
                l.shebangs = split("lua luajit");
                l.keywords = words(
                    "and break do else elseif end false for function goto if in local nil not or repeat "
                    "return then true until while");
                l.types = words("print require pairs ipairs table string math os io");
                l.lineComments = {"--"};
                l.blockOpen = "--[[";
                l.blockClose = "]]";
                l.longStrings = {{"[[", "]]"}};
                l.quotes = "\"'";
                t.push_back(l);

                l = Language{};
                l.names = split("markdown md");
                l.exts = split("md markdown");
                l.markdown = true;
                t.push_back(l);

                return t;
            }();
            return table;
        }

        bool identChar(char c) {
            const auto u = static_cast<unsigned char>(c);
            return std::isalnum(u) != 0 || c == '_' || u >= 128;
        }

        bool isBlank(char c) {
            return c == ' ' || c == '\t';
        }

        bool startsWith(const std::string &s, std::size_t at, const std::string &token) {
            return !token.empty() && at <= s.size() && s.compare(at, token.size(), token) == 0;
        }

        std::string lower(std::string s) {
            for (char &c: s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        void fill(std::vector<Kind> *out, std::size_t from, std::size_t to, Kind kind) {
            if (out == nullptr) return;
            for (std::size_t i = from; i < to && i < out->size(); ++i) (*out)[i] = kind;
        }

        bool rawStart(const std::string &line, std::size_t quote, std::size_t &prefixStart,
                      std::string &delim, std::size_t &open) {
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

        Syntax::State highlightMarkdown(const std::string &line, Syntax::State state,
                                        std::vector<Kind> *out) {
            const std::size_t n = line.size();
            if (out != nullptr) out->assign(n, Kind::Normal);

            const std::size_t j = line.find_first_not_of(" \t");

            if (state.mode == kString) {
                fill(out, 0, n, Kind::String);
                if (j != std::string::npos && startsWith(line, j, "```")) {
                    state.mode = kCode;
                    state.delim.clear();
                }
                return state;
            }
            if (j == std::string::npos) return state;

            if (startsWith(line, j, "```")) {
                fill(out, 0, n, Kind::String);
                state.mode = kString;
                state.delim = "```";
                return state;
            }

            if (line[j] == '#') {
                std::size_t k = j;
                while (k < n && line[k] == '#') ++k;
                if (k == n || isBlank(line[k])) {
                    fill(out, 0, n, Kind::Keyword);
                    return state;
                }
            }

            if (line[j] == '>') {
                fill(out, 0, n, Kind::Comment);
                return state;
            }

            std::size_t i = j;
            if ((line[j] == '-' || line[j] == '*' || line[j] == '+') && j + 1 < n && isBlank(line[j + 1])) {
                fill(out, j, j + 1, Kind::Number);
                i = j + 1;
            }

            while (i < n) {
                if (line[i] == '`') {
                    const std::size_t close = line.find('`', i + 1);
                    if (close != std::string::npos) {
                        fill(out, i, close + 1, Kind::String);
                        i = close + 1;
                        continue;
                    }
                } else if (startsWith(line, i, "**")) {
                    const std::size_t close = line.find("**", i + 2);
                    if (close != std::string::npos) {
                        fill(out, i, close + 2, Kind::Type);
                        i = close + 2;
                        continue;
                    }
                }
                ++i;
            }
            return state;
        }

        Syntax::State highlightLine(const Language &lang, const std::string &line, Syntax::State state,
                                    std::vector<Kind> *out) {
            if (lang.markdown) return highlightMarkdown(line, state, out);

            const std::size_t n = line.size();
            if (out != nullptr) out->assign(n, Kind::Normal);
            std::size_t i = 0;

            if (state.mode == kBlock) {
                const std::size_t end = line.find(lang.blockClose);
                if (end == std::string::npos) {
                    fill(out, 0, n, Kind::Comment);
                    return state;
                }
                fill(out, 0, end + lang.blockClose.size(), Kind::Comment);
                i = end + lang.blockClose.size();
                state.mode = kCode;
            } else if (state.mode == kString) {
                const std::size_t end = line.find(state.delim);
                if (end == std::string::npos) {
                    fill(out, 0, n, Kind::String);
                    return state;
                }
                fill(out, 0, end + state.delim.size(), Kind::String);
                i = end + state.delim.size();
                state.mode = kCode;
                state.delim.clear();
            }

            if (lang.preproc && i == 0) {
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

            auto lineCommentAt = [&](std::size_t at) {
                for (const std::string &lc: lang.lineComments) {
                    if (!startsWith(line, at, lc)) continue;
                    if (lang.hashAfterSpace && lc == "#" && at > 0) {
                        const char p = line[at - 1];
                        if (!isBlank(p) && std::string(";|&(").find(p) == std::string::npos) continue;
                    }
                    return true;
                }
                return false;
            };

            while (i < n) {
                const char c = line[i];
                const char next = i + 1 < n ? line[i + 1] : '\0';

                if (startsWith(line, i, lang.blockOpen)) {
                    const std::size_t end = line.find(lang.blockClose, i + lang.blockOpen.size());
                    if (end == std::string::npos) {
                        fill(out, i, n, Kind::Comment);
                        state.mode = kBlock;
                        return state;
                    }
                    fill(out, i, end + lang.blockClose.size(), Kind::Comment);
                    i = end + lang.blockClose.size();
                    continue;
                }

                if (lineCommentAt(i)) {
                    fill(out, i, n, Kind::Comment);
                    break;
                }

                bool handled = false;
                for (const auto &pair: lang.longStrings) {
                    if (!startsWith(line, i, pair.first)) continue;
                    const std::size_t end = line.find(pair.second, i + pair.first.size());
                    if (end == std::string::npos) {
                        fill(out, i, n, Kind::String);
                        state.mode = kString;
                        state.delim = pair.second;
                        return state;
                    }
                    fill(out, i, end + pair.second.size(), Kind::String);
                    i = end + pair.second.size();
                    handled = true;
                    break;
                }
                if (handled) continue;

                if (lang.attributes && c == '#' &&
                    (next == '[' || (next == '!' && i + 2 < n && line[i + 2] == '['))) {
                    const std::size_t close = line.find(']', i);
                    const std::size_t stop = close == std::string::npos ? n : close + 1;
                    fill(out, i, stop, Kind::Preproc);
                    i = stop;
                    continue;
                }

                if (lang.decorators && c == '@' && std::isalpha(static_cast<unsigned char>(next)) != 0) {
                    std::size_t j = i + 1;
                    while (j < n && (identChar(line[j]) || line[j] == '.')) ++j;
                    fill(out, i, j, Kind::Preproc);
                    i = j;
                    continue;
                }

                if (lang.variables && c == '$') {
                    std::size_t j = i + 1;
                    if (j < n && line[j] == '{') {
                        const std::size_t close = line.find('}', j);
                        j = close == std::string::npos ? n : close + 1;
                    } else if (j < n && (std::isalpha(static_cast<unsigned char>(line[j])) != 0 ||
                                         line[j] == '_')) {
                        while (j < n && (std::isalnum(static_cast<unsigned char>(line[j])) != 0 ||
                                         line[j] == '_')) {
                            ++j;
                        }
                    } else if (j < n && std::string("?#@*!$-0123456789").find(line[j]) != std::string::npos) {
                        ++j;
                    }
                    fill(out, i, j, Kind::Type);
                    i = j > i ? j : i + 1;
                    continue;
                }

                if (lang.quotes.find(c) != std::string::npos) {
                    if (lang.rawStrings && c == '"') {
                        std::size_t prefixStart = 0;
                        std::size_t open = 0;
                        std::string delim;
                        if (rawStart(line, i, prefixStart, delim, open)) {
                            const std::string closing = ")" + delim + "\"";
                            const std::size_t end = line.find(closing, open + 1);
                            if (end == std::string::npos) {
                                fill(out, prefixStart, n, Kind::String);
                                state.mode = kString;
                                state.delim = closing;
                                return state;
                            }
                            fill(out, prefixStart, end + closing.size(), Kind::String);
                            i = end + closing.size();
                            continue;
                        }
                    }

                    if (lang.charLiterals && c == '\'') {
                        const std::size_t len = charLength(line, i + 1);
                        const bool literal =
                                i + 1 < n && (line[i + 1] == '\\' ||
                                              (len > 0 && i + 1 + len < n && line[i + 1 + len] == '\''));
                        if (!literal) {
                            ++i;
                            continue;
                        }
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
                const bool dotDigit = c == '.' && std::isdigit(static_cast<unsigned char>(next)) != 0;
                if (lang.numbers && (digit || dotDigit)) {
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
                    if (lang.keywords.count(word) != 0) {
                        fill(out, i, j, Kind::Keyword);
                    } else if (lang.types.count(word) != 0) {
                        fill(out, i, j, Kind::Type);
                    }
                    i = j;
                    continue;
                }

                ++i;
            }

            return state;
        }

        std::string interpreter(const std::string &first) {
            std::istringstream in(first.substr(2));
            auto base = [](const std::string &s) {
                const std::size_t slash = s.rfind('/');
                return slash == std::string::npos ? s : s.substr(slash + 1);
            };

            std::string w;
            in >> w;
            std::string name = base(w);
            if (name == "env") {
                while (in >> w) {
                    if (!w.empty() && w[0] == '-') continue;
                    name = base(w);
                    break;
                }
            }
            while (!name.empty() &&
                   (std::isdigit(static_cast<unsigned char>(name.back())) != 0 || name.back() == '.')) {
                name.pop_back();
            }
            return name;
        }

        int kindIndex(const std::string &name) {
            if (name == "keyword") return 1;
            if (name == "type") return 2;
            if (name == "string") return 3;
            if (name == "comment") return 4;
            if (name == "number") return 5;
            if (name == "preproc") return 6;
            return -1;
        }

        bool plainName(const std::string &name) {
            return name == "none" || name == "text" || name == "plain";
        }
    }

    Syntax::Syntax() : colors_(kDefaults) {
    }

    void Syntax::setFile(const std::string &path) {
        lang_ = nullptr;
        const std::size_t dot = path.rfind('.');
        const std::size_t slash = path.find_last_of('/');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
            const std::string ext = lower(path.substr(dot + 1));
            for (const Language &l: languages()) {
                if (std::find(l.exts.begin(), l.exts.end(), ext) != l.exts.end()) {
                    lang_ = &l;
                    break;
                }
            }
        }
        invalidate(0);
    }

    void Syntax::sniff(const std::string &firstLine) {
        if (lang_ != nullptr || firstLine.size() < 3 || firstLine.compare(0, 2, "#!") != 0) return;

        const std::string name = interpreter(firstLine);
        for (const Language &l: languages()) {
            if (std::find(l.shebangs.begin(), l.shebangs.end(), name) != l.shebangs.end()) {
                lang_ = &l;
                invalidate(0);
                return;
            }
        }
    }

    bool Syntax::setLanguage(const std::string &name) {
        const std::string wanted = lower(name);
        if (plainName(wanted)) {
            lang_ = nullptr;
            invalidate(0);
            return true;
        }
        for (const Language &l: languages()) {
            if (std::find(l.names.begin(), l.names.end(), wanted) != l.names.end()) {
                lang_ = &l;
                invalidate(0);
                return true;
            }
        }
        return false;
    }

    bool Syntax::knows(const std::string &name) const {
        const std::string wanted = lower(name);
        if (plainName(wanted)) return true;
        for (const Language &l: languages()) {
            if (std::find(l.names.begin(), l.names.end(), wanted) != l.names.end()) return true;
        }
        return false;
    }

    std::string Syntax::languageName() const {
        return lang_ != nullptr ? lang_->names.front() : "none";
    }

    void Syntax::setEnabled(bool enabled) {
        enabled_ = enabled;
    }

    bool Syntax::enabled() const {
        return enabled_;
    }

    bool Syntax::active() const {
        return enabled_ && lang_ != nullptr;
    }

    bool Syntax::setColor(const std::string &name, int code) {
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

    const std::vector<Kind> &Syntax::kinds(const TextBuffer &buffer, std::size_t row) const {
        if (lang_ == nullptr || row >= buffer.lineCount()) {
            scratch_.clear();
            return scratch_;
        }

        if (starts_.empty()) starts_.emplace_back();
        while (starts_.size() <= row) {
            const std::size_t r = starts_.size() - 1;
            State next = highlightLine(*lang_, buffer.line(r), starts_[r], nullptr);
            starts_.push_back(std::move(next));
        }

        highlightLine(*lang_, buffer.line(row), starts_[row], &scratch_);
        return scratch_;
    }

    std::string Syntax::sgr(Kind kind) const {
        const auto index = static_cast<std::size_t>(kind);
        if (index == 0 || index >= kKindCount) return "";
        return "\x1b[38;5;" + std::to_string(colors_[index]) + "m";
    }
}
