#include "text/bidi.h"

#include <algorithm>

namespace qalam {
    namespace {
        enum class Cls { L, R, Digit, Neutral };

        struct Unit {
            std::size_t begin = 0;
            std::size_t end = 0;
            Cls cls = Cls::Neutral;
        };

        struct Decoded {
            char32_t cp = 0xFFFD;
            std::size_t len = 1;
        };

        Decoded decode(const std::string &s, std::size_t i) {
            const auto b = static_cast<unsigned char>(s[i]);
            if (b < 0x80) return {b, 1};

            std::size_t len = 0;
            char32_t cp = 0;
            if (b >= 0xC2 && b <= 0xDF) {
                len = 2;
                cp = b & 0x1F;
            } else if (b >= 0xE0 && b <= 0xEF) {
                len = 3;
                cp = b & 0x0F;
            } else if (b >= 0xF0 && b <= 0xF4) {
                len = 4;
                cp = b & 0x07;
            } else {
                return {};
            }

            if (i + len > s.size()) return {};
            for (std::size_t k = 1; k < len; ++k) {
                const auto c = static_cast<unsigned char>(s[i + k]);
                if ((c & 0xC0) != 0x80) return {};
                cp = (cp << 6) | (c & 0x3F);
            }
            return {cp, len};
        }

        bool inRange(char32_t c, char32_t lo, char32_t hi) {
            return c >= lo && c <= hi;
        }

        bool isMark(char32_t c) {
            return inRange(c, 0x300, 0x36F) || inRange(c, 0x483, 0x489) || inRange(c, 0x591, 0x5BD) ||
                   c == 0x5BF || inRange(c, 0x5C1, 0x5C2) || inRange(c, 0x5C4, 0x5C5) || c == 0x5C7 ||
                   inRange(c, 0x610, 0x61A) || inRange(c, 0x64B, 0x65F) || c == 0x670 ||
                   inRange(c, 0x6D6, 0x6DC) || inRange(c, 0x6DF, 0x6E4) || inRange(c, 0x6E7, 0x6E8) ||
                   inRange(c, 0x6EA, 0x6ED) || c == 0x200C || c == 0x200D ||
                   inRange(c, 0x20D0, 0x20FF) || inRange(c, 0xFE00, 0xFE0F);
        }

        bool isDigit(char32_t c) {
            return inRange(c, '0', '9') || inRange(c, 0x660, 0x669) || inRange(c, 0x6F0, 0x6F9);
        }

        Cls classify(char32_t c) {
            if (isDigit(c)) return Cls::Digit;
            if (c == 0x60C || c == 0x61B || c == 0x61F) return Cls::Neutral;
            if (inRange(c, 0x590, 0x8FF) || inRange(c, 0xFB1D, 0xFDFF) || inRange(c, 0xFE70, 0xFEFF)) {
                return Cls::R;
            }
            if (c < 0x80) {
                const bool alpha = inRange(c, 'a', 'z') || inRange(c, 'A', 'Z');
                return alpha ? Cls::L : Cls::Neutral;
            }
            if (inRange(c, 0x80, 0xBF) || inRange(c, 0x2000, 0x206F) || inRange(c, 0x2190, 0x2BFF) ||
                inRange(c, 0x3000, 0x3003) || c == 0xFFFD) {
                return Cls::Neutral;
            }
            return Cls::L;
        }

        std::vector<Unit> makeUnits(const std::string &line) {
            std::vector<Unit> units;
            std::size_t i = 0;
            while (i < line.size()) {
                const Decoded d = decode(line, i);
                if (isMark(d.cp) && !units.empty()) {
                    units.back().end = i + d.len;
                } else {
                    units.push_back({i, i + d.len, classify(d.cp)});
                }
                i += d.len;
            }
            return units;
        }
    }

    std::vector<VisualRun> visualRuns(const std::string &line) {
        const std::vector<Unit> units = makeUnits(line);
        const std::size_t n = units.size();
        if (n == 0) return {};

        bool rtlPara = false;
        for (const Unit &u: units) {
            if (u.cls == Cls::L || u.cls == Cls::R) {
                rtlPara = u.cls == Cls::R;
                break;
            }
        }
        const int base = rtlPara ? 1 : 0;
        const int paraDir = rtlPara ? 1 : 0;

        auto strongDir = [](Cls c) { return c == Cls::L ? 0 : 1; };

        std::vector<int> left(n, paraDir);
        std::vector<int> right(n, paraDir);
        int last = paraDir;
        for (std::size_t i = 0; i < n; ++i) {
            left[i] = last;
            if (units[i].cls != Cls::Neutral) last = strongDir(units[i].cls);
        }
        last = paraDir;
        for (std::size_t i = n; i-- > 0;) {
            right[i] = last;
            if (units[i].cls != Cls::Neutral) last = strongDir(units[i].cls);
        }

        std::vector<int> level(n, 0);
        int lastLetter = paraDir;
        for (std::size_t i = 0; i < n; ++i) {
            switch (units[i].cls) {
                case Cls::L:
                    level[i] = base == 0 ? 0 : 2;
                    lastLetter = 0;
                    break;
                case Cls::R:
                    level[i] = 1;
                    lastLetter = 1;
                    break;
                case Cls::Digit:
                    level[i] = (base == 1 || lastLetter == 1) ? 2 : 0;
                    break;
                case Cls::Neutral: {
                    const int dir = left[i] == right[i] ? left[i] : paraDir;
                    level[i] = dir == 1 ? 1 : (base == 0 ? 0 : 2);
                    break;
                }
            }
        }

        std::vector<std::size_t> order(n);
        for (std::size_t i = 0; i < n; ++i) order[i] = i;

        int highest = 0;
        int lowestOdd = 1000;
        for (int v: level) {
            highest = std::max(highest, v);
            if (v % 2 == 1) lowestOdd = std::min(lowestOdd, v);
        }

        if (lowestOdd != 1000) {
            for (int lvl = highest; lvl >= lowestOdd; --lvl) {
                std::size_t k = 0;
                while (k < n) {
                    if (level[order[k]] < lvl) {
                        ++k;
                        continue;
                    }
                    std::size_t end = k;
                    while (end < n && level[order[end]] >= lvl) ++end;
                    std::reverse(order.begin() + static_cast<std::ptrdiff_t>(k),
                                 order.begin() + static_cast<std::ptrdiff_t>(end));
                    k = end;
                }
            }
        }

        std::vector<VisualRun> runs;
        std::size_t i = 0;
        while (i < n) {
            const bool rtl = level[order[i]] % 2 == 1;
            std::size_t j = i;
            while (j + 1 < n) {
                const std::size_t cur = order[j];
                const std::size_t nxt = order[j + 1];
                if ((level[nxt] % 2 == 1) != rtl) break;
                const bool contiguous = rtl ? (nxt + 1 == cur) : (nxt == cur + 1);
                if (!contiguous) break;
                ++j;
            }

            const Unit &a = units[order[i]];
            const Unit &b = units[order[j]];
            VisualRun run;
            run.rtl = rtl;
            run.begin = rtl ? b.begin : a.begin;
            run.end = rtl ? a.end : b.end;
            runs.push_back(run);
            i = j + 1;
        }
        return runs;
    }
}
