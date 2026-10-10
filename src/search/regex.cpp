#include "search/regex.h"

#include <algorithm>
#include <bitset>
#include <cctype>
#include <functional>
#include <vector>

#include "text/columns.h"

namespace qalam {
namespace {

constexpr long kBudget = 200000;

bool isWordChar(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '_' || u >= 128;
}

char foldChar(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

unsigned byteOf(char c) {
    return static_cast<unsigned char>(c);
}

struct Node {
    enum class Type { Char, Any, Set, Bol, Eol, WordBegin, WordEnd, Group, Repeat };

    Type type = Type::Char;
    char ch = 0;
    std::bitset<256> set;
    int group = 0;
    int min = 0;
    int max = -1;
    std::vector<std::vector<Node>> alts;
};

using Alts = std::vector<std::vector<Node>>;

void classEscape(std::bitset<256>& set, char e) {
    std::bitset<256> base;
    const char lower = foldChar(e);
    if (lower == 'w') {
        for (unsigned v = 0; v < 256; ++v) {
            if (isWordChar(static_cast<char>(v))) base.set(v);
        }
    } else if (lower == 'd') {
        for (unsigned v = '0'; v <= '9'; ++v) base.set(v);
    } else {
        base.set(' ');
        base.set('\t');
        base.set('\r');
        base.set('\n');
        base.set('\f');
        base.set('\v');
    }
    if (std::isupper(static_cast<unsigned char>(e)) != 0) base.flip();
    set |= base;
}

class Parser {
public:
    Parser(const std::string& pattern, bool fold) : p_(pattern), fold_(fold) {}

    bool parse(Alts& out, int& groups, std::string& error) {
        out = parseAlts(0);
        if (!error_.empty()) {
            error = error_;
            return false;
        }
        groups = groups_;
        return true;
    }

private:
    void fail(const std::string& message) {
        if (error_.empty()) error_ = message;
    }

    Alts parseAlts(int depth) {
        Alts alts(1);
        while (pos_ < p_.size() && error_.empty()) {
            const char c = p_[pos_];
            if (c == '|') {
                ++pos_;
                alts.emplace_back();
                continue;
            }
            if (c == ')') {
                if (depth == 0) fail("unmatched )");
                break;
            }
            std::optional<Node> atom = parseAtom(depth);
            if (!atom) break;
            alts.back().push_back(quantified(std::move(*atom)));
        }
        return alts;
    }

    Node quantified(Node atom) {
        if (pos_ >= p_.size()) return atom;

        int lo = 0;
        int hi = -1;
        switch (p_[pos_]) {
            case '*': break;
            case '+': lo = 1; break;
            case '?': hi = 1; break;
            default: return atom;
        }
        ++pos_;

        Node repeat;
        repeat.type = Node::Type::Repeat;
        repeat.min = lo;
        repeat.max = hi;
        repeat.alts.emplace_back();
        repeat.alts[0].push_back(std::move(atom));
        return repeat;
    }

    std::optional<Node> parseAtom(int depth) {
        const char c = p_[pos_++];
        Node n;

        switch (c) {
            case '*':
            case '+':
            case '?':
                fail("nothing to repeat");
                return std::nullopt;
            case '.':
                n.type = Node::Type::Any;
                return n;
            case '^':
                n.type = Node::Type::Bol;
                return n;
            case '$':
                n.type = Node::Type::Eol;
                return n;
            case '(': {
                if (groups_ >= 9) {
                    fail("too many groups");
                    return std::nullopt;
                }
                n.type = Node::Type::Group;
                n.group = ++groups_;
                n.alts = parseAlts(depth + 1);
                if (!error_.empty()) return std::nullopt;
                if (pos_ >= p_.size() || p_[pos_] != ')') {
                    fail("missing )");
                    return std::nullopt;
                }
                ++pos_;
                return n;
            }
            case '[':
                return parseClass();
            case '\\':
                return parseEscape();
            default:
                n.type = Node::Type::Char;
                n.ch = c;
                return n;
        }
    }

    std::optional<Node> parseEscape() {
        if (pos_ >= p_.size()) {
            fail("trailing backslash");
            return std::nullopt;
        }

        const char e = p_[pos_++];
        Node n;
        switch (e) {
            case '<':
                n.type = Node::Type::WordBegin;
                return n;
            case '>':
                n.type = Node::Type::WordEnd;
                return n;
            case 'w':
            case 'W':
            case 'd':
            case 'D':
            case 's':
            case 'S':
                n.type = Node::Type::Set;
                classEscape(n.set, e);
                return n;
            case 't':
                n.type = Node::Type::Char;
                n.ch = '\t';
                return n;
            default:
                n.type = Node::Type::Char;
                n.ch = e;
                return n;
        }
    }

    std::optional<Node> parseClass() {
        std::bitset<256> set;
        bool negate = false;
        if (pos_ < p_.size() && p_[pos_] == '^') {
            negate = true;
            ++pos_;
        }

        bool first = true;
        bool closed = false;
        while (pos_ < p_.size()) {
            char c = p_[pos_++];
            if (c == ']' && !first) {
                closed = true;
                break;
            }
            first = false;

            if (c == '\\' && pos_ < p_.size()) {
                const char e = p_[pos_++];
                if (std::string("wWdDsS").find(e) != std::string::npos) {
                    classEscape(set, e);
                    continue;
                }
                c = e == 't' ? '\t' : e;
            }

            if (pos_ + 1 < p_.size() && p_[pos_] == '-' && p_[pos_ + 1] != ']') {
                char hi = p_[pos_ + 1];
                pos_ += 2;
                if (hi == '\\' && pos_ < p_.size()) hi = p_[pos_++];
                if (byteOf(c) > byteOf(hi)) {
                    fail("invalid range");
                    return std::nullopt;
                }
                for (unsigned v = byteOf(c); v <= byteOf(hi); ++v) set.set(v);
            } else {
                set.set(byteOf(c));
            }
        }

        if (!closed) {
            fail("missing ]");
            return std::nullopt;
        }

        if (fold_) {
            for (unsigned v = 'a'; v <= 'z'; ++v) {
                if (set.test(v) || set.test(v - 32)) {
                    set.set(v);
                    set.set(v - 32);
                }
            }
        }
        if (negate) set.flip();

        Node n;
        n.type = Node::Type::Set;
        n.set = set;
        return n;
    }

    const std::string& p_;
    bool fold_;
    std::size_t pos_ = 0;
    int groups_ = 0;
    std::string error_;
};

class Matcher {
public:
    using Cont = std::function<bool(std::size_t)>;

    Matcher(const std::string& text, bool fold) : s_(text), fold_(fold) {}

    Match caps;

    bool alts(const Alts& list, std::size_t pos, const Cont& k) {
        for (const std::vector<Node>& sequence : list) {
            if (seq(sequence, 0, pos, k)) return true;
        }
        return false;
    }

private:
    bool same(char a, char b) const {
        return fold_ ? foldChar(a) == foldChar(b) : a == b;
    }

    bool seq(const std::vector<Node>& v, std::size_t i, std::size_t pos, const Cont& k) {
        if (i == v.size()) return k(pos);
        return node(v[i], pos, [&](std::size_t p) { return seq(v, i + 1, p, k); });
    }

    bool node(const Node& n, std::size_t pos, const Cont& k) {
        const std::size_t size = s_.size();

        switch (n.type) {
            case Node::Type::Char:
                return pos < size && same(s_[pos], n.ch) && k(pos + 1);
            case Node::Type::Any:
                if (pos >= size) return false;
                return k(pos + charLength(s_, pos));
            case Node::Type::Set:
                if (pos >= size || !n.set.test(byteOf(s_[pos]))) return false;
                return k(pos + charLength(s_, pos));
            case Node::Type::Bol:
                return pos == 0 && k(pos);
            case Node::Type::Eol:
                return pos == size && k(pos);
            case Node::Type::WordBegin:
                return pos < size && isWordChar(s_[pos]) &&
                       (pos == 0 || !isWordChar(s_[pos - 1])) && k(pos);
            case Node::Type::WordEnd:
                return pos > 0 && isWordChar(s_[pos - 1]) &&
                       (pos == size || !isWordChar(s_[pos])) && k(pos);
            case Node::Type::Group: {
                const auto g = static_cast<std::size_t>(n.group);
                return alts(n.alts, pos, [&](std::size_t p) {
                    const auto saved = caps.groups[g];
                    caps.groups[g] = {pos, p};
                    if (k(p)) return true;
                    caps.groups[g] = saved;
                    return false;
                });
            }
            case Node::Type::Repeat:
                return repeat(n, 0, pos, k);
        }
        return false;
    }

    bool repeat(const Node& n, int count, std::size_t pos, const Cont& k) {
        if (--budget_ <= 0) return false;

        const Node& child = n.alts[0][0];
        if (n.max < 0 || count < n.max) {
            const bool ok = node(child, pos, [&](std::size_t p) {
                if (p == pos && count >= n.min) return false;
                return repeat(n, count + 1, p, k);
            });
            if (ok) return true;
        }
        return count >= n.min && k(pos);
    }

    const std::string& s_;
    bool fold_;
    long budget_ = kBudget;
};

}

struct Regex::Impl {
    Alts alts;
    int groups = 0;
    bool fold = false;
};

std::optional<Regex> Regex::compile(const std::string& pattern, bool ignoreCase,
                                    std::string& error) {
    auto impl = std::make_shared<Impl>();
    Parser parser(pattern, ignoreCase);
    if (!parser.parse(impl->alts, impl->groups, error)) return std::nullopt;
    impl->fold = ignoreCase;

    Regex regex;
    regex.impl_ = std::move(impl);
    return regex;
}

std::optional<Match> Regex::search(const std::string& text, std::size_t from) const {
    const Impl& im = *impl_;
    std::size_t start = from;

    while (start <= text.size()) {
        Matcher matcher(text, im.fold);
        std::size_t end = 0;
        const bool found = matcher.alts(im.alts, start, [&](std::size_t p) {
            end = p;
            return true;
        });

        if (found) {
            Match result = matcher.caps;
            result.begin = start;
            result.end = end;
            result.groups[0] = {start, end};
            return result;
        }

        if (start == text.size()) break;
        start += std::max<std::size_t>(1, charLength(text, start));
    }
    return std::nullopt;
}

}