#include <string>
#include <vector>

#include "buffer/line_buffer.h"
#include "check.h"
#include "undo/history.h"

using namespace qalam;

namespace {
    struct Model {
        std::string doc;

        std::vector<std::string> lines() const {
            std::vector<std::string> out;
            std::size_t begin = 0;
            while (true) {
                const std::size_t end = doc.find('\n', begin);
                if (end == std::string::npos) {
                    out.push_back(doc.substr(begin));
                    break;
                }
                out.push_back(doc.substr(begin, end - begin));
                begin = end + 1;
            }
            return out;
        }

        std::size_t offset(Pos p) const {
            std::size_t off = 0;
            for (std::size_t r = 0; r < p.row; ++r) off = doc.find('\n', off) + 1;
            return off + p.col;
        }

        void replace(Pos start, Pos end, const std::string &inserted) {
            const std::size_t a = offset(start);
            const std::size_t b = offset(end);
            doc.replace(a, b - a, inserted);
        }
    };

    bool same(const LineBuffer &b, const Model &m) {
        const std::vector<std::string> expected = m.lines();
        if (b.lineCount() != expected.size()) return false;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (b.line(i) != expected[i]) return false;
        }
        return true;
    }

    std::string bigText(std::size_t lines) {
        std::string text;
        for (std::size_t i = 0; i < lines; ++i) {
            text += "line " + std::to_string(i);
            if (i + 1 < lines) text += '\n';
        }
        return text;
    }

    unsigned gSeed = 99;

    unsigned next() {
        gSeed = gSeed * 1103515245u + 12345u;
        return gSeed >> 8;
    }
}

TEST(linebuffer_random_edits_match_a_plain_string_model) {
    LineBuffer b;
    Model m;
    const std::string start = bigText(3000);
    b.replace({0, 0}, {0, 0}, start);
    m.replace({0, 0}, {0, 0}, start);
    CHECK(same(b, m));

    const std::vector<std::string> inserts = {
        "", "x", "\n", "ab\ncd",
        "\n\n\n", "yz", "one\ntwo\n", bigText(700)
    };

    for (int step = 0; step < 600; ++step) {
        const std::vector<std::string> lines = m.lines();
        const std::size_t r1 = next() % lines.size();
        const std::size_t c1 = next() % (lines[r1].size() + 1);

        std::size_t span = 0;
        switch (next() % 4) {
            case 0: span = 0;
                break;
            case 1: span = next() % 3;
                break;
            case 2: span = next() % 40;
                break;
            default: span = next() % 700;
                break;
        }
        const std::size_t r2 = std::min(r1 + span, lines.size() - 1);
        std::size_t c2 = next() % (lines[r2].size() + 1);
        if (r2 == r1 && c2 < c1) c2 = c1;

        const std::string &ins = inserts[next() % inserts.size()];
        const Edit edit = b.replace({r1, c1}, {r2, c2}, ins);
        CHECK_EQ(edit.removed, m.doc.substr(m.offset({r1, c1}), m.offset({r2, c2}) - m.offset({r1, c1})));
        m.replace({r1, c1}, {r2, c2}, ins);

        CHECK_EQ(b.lineCount(), m.lines().size());
        if (step % 25 == 0 && !same(b, m)) {
            CHECK(false);
            return;
        }
    }
    CHECK(same(b, m));
}

TEST(linebuffer_large_insert_and_cross_chunk_delete) {
    LineBuffer b;
    b.replace({0, 0}, {0, 0}, bigText(100000));
    CHECK_EQ(b.lineCount(), std::size_t{100000});
    CHECK_EQ(b.line(0), std::string("line 0"));
    CHECK_EQ(b.line(99999), std::string("line 99999"));
    CHECK_EQ(b.line(54321), std::string("line 54321"));

    b.replace({10, 2}, {99990, 3}, "");
    CHECK_EQ(b.lineCount(), std::size_t{10});
    CHECK_EQ(b.line(10 - 1), std::string("line 9"));
    CHECK_EQ(b.line(0), std::string("line 0"));
}

TEST(linebuffer_delete_everything_leaves_one_empty_line) {
    LineBuffer b;
    b.replace({0, 0}, {0, 0}, bigText(5000));
    b.replace({0, 0}, {4999, b.line(4999).size()}, "");
    CHECK_EQ(b.lineCount(), std::size_t{1});
    CHECK(b.line(0).empty());
    b.replace({0, 0}, {0, 0}, "again");
    CHECK_EQ(b.line(0), std::string("again"));
}

TEST(linebuffer_assign_spans_chunks) {
    LineBuffer b;
    std::vector<std::string> lines;
    for (int i = 0; i < 1000; ++i) lines.push_back("r" + std::to_string(i));
    b.assign(lines);
    CHECK_EQ(b.lineCount(), std::size_t{1000});
    CHECK_EQ(b.line(255), std::string("r255"));
    CHECK_EQ(b.line(256), std::string("r256"));
    CHECK_EQ(b.line(999), std::string("r999"));

    b.assign({});
    CHECK_EQ(b.lineCount(), std::size_t{1});
}

TEST(linebuffer_edits_at_chunk_boundaries) {
    LineBuffer b;
    Model m;
    const std::string start = bigText(2000);
    b.replace({0, 0}, {0, 0}, start);
    m.replace({0, 0}, {0, 0}, start);

    for (std::size_t row: {0u, 255u, 256u, 257u, 511u, 512u, 1999u}) {
        b.replace({row, 0}, {row, 0}, "A\nB");
        m.replace({row, 0}, {row, 0}, "A\nB");
        CHECK(same(b, m));
        const std::size_t last = b.lineCount() - 1;
        const std::size_t r = std::min<std::size_t>(row + 1, last);
        b.replace({r, 0}, {std::min(r + 1, last), 0}, "");
        m.replace({r, 0}, {std::min(r + 1, last), 0}, "");
        CHECK(same(b, m));
    }
}

TEST(linebuffer_in_place_edit_keeps_other_lines) {
    LineBuffer b;
    b.replace({0, 0}, {0, 0}, "aaa\nbbb\nccc");
    b.replace({1, 1}, {1, 2}, "XYZ");
    CHECK_EQ(b.line(0), std::string("aaa"));
    CHECK_EQ(b.line(1), std::string("bXYZb"));
    CHECK_EQ(b.line(2), std::string("ccc"));
    CHECK_EQ(b.lineCount(), std::size_t{3});
}

TEST(history_reports_the_lowest_touched_row) {
    LineBuffer b;
    UndoHistory h;
    b.replace({0, 0}, {0, 0}, bigText(50));

    h.record(b.replace({30, 0}, {30, 0}, "x"));
    h.record(b.replace({10, 0}, {10, 0}, "y"));
    h.close();

    h.undo(b);
    CHECK_EQ(h.touchedRow(), std::size_t{10});
    h.redo(b);
    CHECK_EQ(h.touchedRow(), std::size_t{10});

    h.record(b.replace({40, 0}, {40, 0}, "z"));
    h.close();
    h.undo(b);
    CHECK_EQ(h.touchedRow(), std::size_t{40});
}
