#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "buffer/file_io.h"
#include "buffer/line_buffer.h"
#include "editor/editor.h"
#include "input/key_notation.h"
#include "search/search.h"
#include "syntax/syntax.h"
#include "text/columns.h"
#include "undo/history.h"

using namespace qalam;
using Clock = std::chrono::steady_clock;

namespace {
    unsigned gSeed = 2024;

    unsigned next() {
        gSeed = gSeed * 1103515245u + 12345u;
        return gSeed >> 8;
    }

    void report(const char *name, std::size_t ops, double ms) {
        const double perSecond = ms > 0 ? static_cast<double>(ops) / (ms / 1000.0) : 0;
        std::printf("%-44s %9.1f ms  %12.0f ops/s\n", name, ms, perSecond);
        std::fflush(stdout);
    }

    double timeIt(const std::function<void()> &fn) {
        const auto start = Clock::now();
        fn();
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    std::string makeSource(std::size_t lines) {
        std::string text;
        text.reserve(lines * 40);
        for (std::size_t i = 0; i < lines; ++i) {
            switch (i % 5) {
                case 0: text += "int value" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
                    break;
                case 1: text += "    // comment about line " + std::to_string(i) + "\n";
                    break;
                case 2: text += "if (x > " + std::to_string(i) + ") { return foo(\"s\"); }\n";
                    break;
                case 3: text += "\n";
                    break;
                default: text += "const char* name = \"item\";\n";
                    break;
            }
        }
        text += "needle_marker();\n";
        return text;
    }

    LineBuffer fill(const std::string &text) {
        LineBuffer b;
        b.replace({0, 0}, {0, 0}, text);
        return b;
    }

    void feed(Editor &ed, const std::string &keys) {
        std::string error;
        const auto parsed = parseKeys(keys, error);
        if (!parsed) {
            std::printf("bad keys: %s\n", keys.c_str());
            std::exit(1);
        }
        for (const Key &key: *parsed) ed.handleKey(key);
    }
}

int main(int argc, char **argv) {
    const std::size_t lines = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 200000;
    const std::size_t edits = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 50000;
    std::printf("qalam benchmark: %zu lines, %zu edits per case\n\n", lines, edits);

    const std::string source = makeSource(lines);
    std::printf("source size: %.1f MB\n\n", static_cast<double>(source.size()) / 1e6);

    {
        LineBuffer b;
        report("buffer: insert whole file", lines,
               timeIt([&] { b.replace({0, 0}, {0, 0}, source); }));
    }

    {
        LineBuffer b = fill(source);
        const std::size_t n = b.lineCount();
        report("buffer: random 1-char inserts", edits, timeIt([&] {
            for (std::size_t i = 0; i < edits; ++i) {
                const std::size_t row = next() % n;
                b.replace({row, 0}, {row, 0}, "x");
            }
        }));
    }

    {
        LineBuffer b = fill(source);
        report("buffer: random newline inserts", edits, timeIt([&] {
            for (std::size_t i = 0; i < edits; ++i) {
                const std::size_t row = next() % b.lineCount();
                b.replace({row, 0}, {row, 0}, "\n");
            }
        }));
        report("buffer: random line joins", edits, timeIt([&] {
            for (std::size_t i = 0; i < edits; ++i) {
                const std::size_t row = next() % (b.lineCount() - 1);
                b.replace({row, b.line(row).size()}, {row + 1, 0}, "");
            }
        }));
    }

    {
        LineBuffer b = fill(source);
        UndoHistory h;
        const std::size_t n = b.lineCount();
        report("history: record edits (one group each)", edits, timeIt([&] {
            for (std::size_t i = 0; i < edits; ++i) {
                const std::size_t row = next() % n;
                h.record(b.replace({row, 0}, {row, 0}, "x"));
                h.close();
            }
        }));
        report("history: undo them all", edits, timeIt([&] {
            for (std::size_t i = 0; i < edits; ++i) h.undo(b);
        }));
        report("history: redo them all", edits, timeIt([&] {
            for (std::size_t i = 0; i < edits; ++i) h.redo(b);
        }));
    }

    {
        const LineBuffer b = fill(source);
        report("search: forward, rare literal, whole file", 1, timeIt([&] {
            searchForward(b, {0, 0}, "needle_marker");
        }));
        report("search: forward, regex, whole file", 1, timeIt([&] {
            searchForward(b, {0, 0}, "needle_\\w+\\(");
        }));
        report("search: all matches of \\<int\\>", 1, timeIt([&] {
            std::size_t total = 0;
            for (std::size_t r = 0; r < b.lineCount(); ++r) {
                total += findInLine(b.line(r), "\\<int\\>").size();
            }
            if (total == static_cast<std::size_t>(-1)) std::puts("");
        }));
    }

    {
        const LineBuffer b = fill(source);
        Syntax s;
        s.setFile("bench.cpp");
        const std::size_t last = b.lineCount() - 1;
        report("syntax: first paint at last row", 1, timeIt([&] { s.kinds(b, last); }));
        report("syntax: repaint after invalidate(0)", 1, timeIt([&] {
            s.invalidate(0);
            s.kinds(b, last);
        }));
        report("syntax: 24 visible rows, cached", 24 * 1000, timeIt([&] {
            for (int round = 0; round < 1000; ++round) {
                for (std::size_t r = last - 24; r < last; ++r) s.kinds(b, r);
            }
        }));
    }

    {
        const std::filesystem::path path =
                std::filesystem::temp_directory_path() / "qalam_bench.txt";
        {
            const LineBuffer b = fill(source);
            report("file: save", 1, timeIt([&] { saveFile(path.string(), b); }));
        }
        LineBuffer b;
        report("file: load", 1, timeIt([&] { loadFile(path.string(), b); }));
        std::filesystem::remove(path);
    }

    {
        Editor ed(std::make_unique<LineBuffer>());
        Size size;
        size.rows = 40;
        size.cols = 120;
        ed.resize(size);
        ed.setText(source);

        report("editor: 5000 x \"ddp\" at random rows", 5000, timeIt([&] {
            for (int i = 0; i < 5000; ++i) {
                const std::size_t row = next() % (lines / 2) + 1;
                feed(ed, std::to_string(row) + "Gddp");
            }
        }));
        report("editor: 5000 x \"u\" after those", 5000, timeIt([&] {
            for (int i = 0; i < 5000; ++i) feed(ed, "u");
        }));
        report("editor: render 200 frames", 200, timeIt([&] {
            for (int i = 0; i < 200; ++i) {
                feed(ed, "j");
                static_cast<void>(ed.view());
            }
        }));
        report("editor: :%s/int/long/g on whole file", 1,
               timeIt([&] { feed(ed, ":%s/int/long/g<CR>"); }));
        report("editor: u (undo the substitute)", 1, timeIt([&] { feed(ed, "u"); }));
        report("editor: :g/comment/d (20% of lines)", 1,
               timeIt([&] { feed(ed, ":g/comment/d<CR>"); }));
        report("editor: u (undo the :g)", 1, timeIt([&] { feed(ed, "u"); }));
        report("editor: :%normal Ax<Esc> on whole file", 1,
               timeIt([&] { feed(ed, ":%normal Ax<Lt>Esc><CR>"); }));
    }

    return 0;
}
