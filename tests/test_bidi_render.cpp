#include <string>

#include "harness.h"
#include "render/render.h"
#include "text/bidi.h"

using namespace qalam;

namespace {
    Size screen(int rows, int cols) {
        Size size;
        size.rows = rows;
        size.cols = cols;
        return size;
    }

    bool has(const std::string &hay, const std::string &needle) {
        return hay.find(needle) != std::string::npos;
    }

    const std::string kLogical = "ab \xd9\x82\xd9\x84\xd9\x85";
    const std::string kVisual = "ab \xd9\x85\xd9\x84\xd9\x82";
}

TEST(cell_order_is_empty_for_ascii) {
    CHECK(visualCellOrder("hello world").empty());
    CHECK(visualCellOrder("").empty());
}

TEST(cell_order_reverses_the_arabic_run) {
    const std::vector<std::size_t> order = visualCellOrder(kLogical);
    const std::vector<std::size_t> expected = {0, 1, 2, 5, 4, 3};
    CHECK(order == expected);
}

TEST(cell_order_keeps_diacritics_with_their_letter) {
    const std::vector<std::size_t> order =
            visualCellOrder("\xd9\x82\xd9\x8e\xd9\x84\xd9\x8e");
    const std::vector<std::size_t> expected = {1, 0};
    CHECK(order == expected);
}

TEST(cell_order_is_always_a_permutation) {
    const std::string pieces[] = {"a", " ", "\xd9\x82", "\xd9\x8e", "\xe6\x97\xa5", "1", "\t", ","};
    unsigned seed = 5;
    for (int round = 0; round < 2000; ++round) {
        std::string line;
        seed = seed * 1103515245u + 12345u;
        const unsigned n = (seed >> 16) % 14;
        for (unsigned i = 0; i < n; ++i) {
            seed = seed * 1103515245u + 12345u;
            line += pieces[(seed >> 16) % 8];
        }
        const std::vector<std::size_t> order = visualCellOrder(line);
        if (order.empty()) continue;
        std::vector<int> seen(order.size(), 0);
        for (std::size_t v: order) {
            CHECK(v < seen.size());
            if (v < seen.size()) ++seen[v];
        }
        for (int s: seen)
            CHECK_EQ(s, 1);
    }
}

TEST(render_keeps_logical_order_without_bidi) {
    Ed ed(kLogical);
    ed.e.resize(screen(10, 40));
    CHECK(has(renderFrame(screen(10, 40), ed.e.view()), kLogical));
}

TEST(render_reorders_with_bidi) {
    Ed ed(kLogical);
    ed.e.resize(screen(10, 40));
    ed.feed(":set bidi<CR>");
    const std::string frame = renderFrame(screen(10, 40), ed.e.view());
    CHECK(has(frame, kVisual));
    CHECK(!has(frame, kLogical));
    ed.feed(":set nobidi<CR>");
    CHECK(has(renderFrame(screen(10, 40), ed.e.view()), kLogical));
}

TEST(bidi_cursor_uses_the_visual_column) {
    Ed ed(kLogical);
    ed.e.resize(screen(10, 40));
    ed.feed(":set bidi<CR>$");
    CHECK_EQ(ed.e.view().windows[0].cursorCol, 3);
    ed.feed("0");
    CHECK_EQ(ed.e.view().windows[0].cursorCol, 0);
}

TEST(bidi_does_not_change_the_text) {
    Ed ed(kLogical);
    ed.feed(":set bidi<CR>x");
    CHECK_EQ(ed.text(), std::string("b \xd9\x82\xd9\x84\xd9\x85"));
}

TEST(bidi_option_is_listed) {
    Ed ed("");
    ed.feed(":set<CR>");
    CHECK(ed.e.message().find("nobidi") != std::string::npos);
    ed.feed(":set bidi<CR>:set<CR>");
    CHECK(ed.e.message().find(" bidi") != std::string::npos);
}

TEST(bidi_render_handles_wrap_and_tiny_screens) {
    Ed ed(kLogical + kLogical + kLogical);
    ed.feed(":set bidi wrap<CR>");
    for (int cols = 1; cols <= 12; ++cols) {
        ed.e.resize(screen(6, cols));
        CHECK(!renderFrame(screen(6, cols), ed.e.view()).empty());
    }
}
