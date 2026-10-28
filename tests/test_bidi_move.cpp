#include <string>

#include "harness.h"

namespace {
    const std::string kLine = "ab \xd9\x82\xd9\x84\xd9\x85";
}

TEST(bidi_l_moves_visually_across_the_direction_change) {
    Ed ed(kLine);
    ed.feed(":set bidi<CR>");
    ed.feed("ll");
    CHECK_EQ(ed.col(), std::size_t{2});
    ed.feed("l");
    CHECK_EQ(ed.col(), std::size_t{7});
    ed.feed("l");
    CHECK_EQ(ed.col(), std::size_t{5});
    ed.feed("l");
    CHECK_EQ(ed.col(), std::size_t{3});
}

TEST(bidi_h_reverses_l) {
    Ed ed(kLine);
    ed.feed(":set bidi<CR>lllll");
    CHECK_EQ(ed.col(), std::size_t{3});
    ed.feed("h");
    CHECK_EQ(ed.col(), std::size_t{5});
    ed.feed("hhhhhhh");
    CHECK_EQ(ed.col(), std::size_t{0});
}

TEST(bidi_stops_at_the_visual_edges) {
    Ed ed(kLine);
    ed.feed(":set bidi<CR>llllllllll");
    CHECK_EQ(ed.col(), std::size_t{3});
    ed.feed("hhhhhhhhhh");
    CHECK_EQ(ed.col(), std::size_t{0});
}

TEST(bidi_count_moves_that_many_screen_cells) {
    Ed ed(kLine);
    ed.feed(":set bidi<CR>4l");
    CHECK_EQ(ed.col(), std::size_t{5});
}

TEST(nobidi_keeps_logical_movement) {
    Ed ed(kLine);
    ed.feed("lll");
    CHECK_EQ(ed.col(), std::size_t{3});
    ed.feed("l");
    CHECK_EQ(ed.col(), std::size_t{5});
}

TEST(bidi_ascii_lines_move_normally) {
    Ed ed("hello");
    ed.feed(":set bidi<CR>lll");
    CHECK_EQ(ed.col(), std::size_t{3});
}

TEST(bidi_operators_stay_logical) {
    Ed ed(kLine);
    ed.feed(":set bidi<CR>x");
    CHECK_EQ(ed.text(), std::string("b \xd9\x82\xd9\x84\xd9\x85"));
    Ed word(kLine);
    word.feed(":set bidi<CR>dw");
    CHECK_EQ(word.text(), std::string("\xd9\x82\xd9\x84\xd9\x85"));
}

TEST(bidi_arrow_keys_follow_screen_direction) {
    Ed ed(kLine);
    ed.feed(":set bidi<CR><Right><Right><Right>");
    CHECK_EQ(ed.col(), std::size_t{7});
    ed.feed("<Left>");
    CHECK_EQ(ed.col(), std::size_t{2});
}

TEST(bidi_wide_and_tab_cells_do_not_stall_movement) {
    Ed ed("\xd9\x82\t\xe6\x97\xa5x");
    ed.feed(":set bidi<CR>");

    const std::size_t start = ed.col();
    ed.feed("l");
    CHECK_EQ(ed.col(), start);

    ed.feed("h");
    CHECK(ed.col() != start);
    const std::size_t afterTab = ed.col();
    ed.feed("h");
    CHECK(ed.col() != afterTab);

    for (int i = 0; i < 20; ++i) ed.feed("h");
    const std::size_t leftEdge = ed.col();
    ed.feed("h");
    CHECK_EQ(ed.col(), leftEdge);

    for (int i = 0; i < 20; ++i) ed.feed("l");
    CHECK_EQ(ed.col(), start);
    CHECK(ed.e.mode() == qalam::Mode::Normal);
}
