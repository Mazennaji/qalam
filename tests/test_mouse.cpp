#include <string>

#include "harness.h"
#include "terminal/decoder.h"

using namespace qalam;

namespace {

    Key mouse(int button, int x, int y, bool press = true) {
        Key key;
        key.type = KeyType::Mouse;
        key.button = button;
        key.x = x;
        key.y = y;
        key.press = press;
        return key;
    }

    void click(Ed& ed, int x, int y) {
        ed.e.handleKey(mouse(0, x, y, true));
        ed.e.handleKey(mouse(0, x, y, false));
    }
}

TEST(decode_mouse_press_release_and_wheel) {
    const std::string press = "\x1b[<0;10;5M";
    const Decoded d = decodeKey(press, false);
    CHECK(d.key.type == KeyType::Mouse);
    CHECK_EQ(d.key.x, 9);
    CHECK_EQ(d.key.y, 4);
    CHECK_EQ(d.key.button, 0);
    CHECK(d.key.press);
    CHECK_EQ(d.used, press.size());

    CHECK(!decodeKey("\x1b[<0;10;5m", false).key.press);
    CHECK_EQ(decodeKey("\x1b[<64;1;1M", false).key.button, 64);
    CHECK_EQ(decodeKey("\x1b[<32;3;2M", false).key.button, 32);
}

TEST(decode_mouse_partial_and_malformed) {
    CHECK(decodeKey("\x1b[<0;1", false).needMore);
    CHECK(decodeKey("\x1b[<0;1;", false).needMore);
    const Decoded bad = decodeKey("\x1b[<0;1M", false);
    CHECK(bad.key.type == KeyType::None);
    CHECK_EQ(bad.used, std::size_t{7});
}

TEST(mouse_click_places_the_cursor) {
    Ed ed("hello\nworld here");
    click(ed, 3, 1);
    CHECK_EQ(ed.row(), std::size_t{1});
    CHECK_EQ(ed.col(), std::size_t{3});

    click(ed, 70, 0);
    CHECK_EQ(ed.row(), std::size_t{0});
    CHECK_EQ(ed.col(), std::size_t{4});

    click(ed, 5, 15);
    CHECK_EQ(ed.row(), std::size_t{1});
}

TEST(mouse_click_accounts_for_the_number_gutter) {
    Ed ed("hello world");
    ed.feed(":set nu<CR>");
    click(ed, 4 + 2, 0);
    CHECK_EQ(ed.col(), std::size_t{2});
    click(ed, 0, 0);
    CHECK_EQ(ed.col(), std::size_t{0});
}

TEST(mouse_click_on_wide_characters) {
    Ed ed("a\xe6\x97\xa5" "b");
    click(ed, 2, 0);
    CHECK_EQ(ed.col(), std::size_t{1});
    click(ed, 3, 0);
    CHECK_EQ(ed.col(), std::size_t{4});
}

TEST(mouse_click_with_wrap) {
    Ed ed(std::string(100, 'a') + "\nnext");
    ed.feed(":set wrap<CR>");
    click(ed, 5, 1);
    CHECK_EQ(ed.row(), std::size_t{0});
    CHECK_EQ(ed.col(), std::size_t{85});
    click(ed, 2, 2);
    CHECK_EQ(ed.row(), std::size_t{1});
    CHECK_EQ(ed.col(), std::size_t{2});
}

TEST(mouse_drag_selects_text) {
    Ed ed("hello world");
    ed.e.handleKey(mouse(0, 0, 0, true));
    ed.e.handleKey(mouse(32, 3, 0, true));
    CHECK(ed.e.mode() == Mode::Visual);
    ed.e.handleKey(mouse(0, 3, 0, false));
    ed.feed("d");
    CHECK_EQ(ed.text(), std::string("o world"));
}

TEST(mouse_click_leaves_visual_mode) {
    Ed ed("hello world");
    ed.feed("v");
    click(ed, 6, 0);
    CHECK(ed.e.mode() == Mode::Normal);
    CHECK_EQ(ed.col(), std::size_t{6});
}

TEST(mouse_wheel_moves_three_lines) {
    std::string text;
    for (int i = 0; i < 100; ++i) text += "line\n";
    Ed ed(text);
    ed.e.handleKey(mouse(65, 0, 0, true));
    CHECK_EQ(ed.row(), std::size_t{3});
    ed.e.handleKey(mouse(65, 0, 0, true));
    CHECK_EQ(ed.row(), std::size_t{6});
    ed.e.handleKey(mouse(64, 0, 0, true));
    CHECK_EQ(ed.row(), std::size_t{3});
}

TEST(mouse_is_ignored_in_prompts) {
    Ed ed("abc");
    ed.feed(":");
    click(ed, 1, 0);
    CHECK(ed.e.mode() == Mode::Command);
    ed.feed("<Esc>");
    CHECK_EQ(ed.col(), std::size_t{0});
}

TEST(mouse_in_split_windows_does_not_corrupt) {
    Ed ed("abc\ndef");
    ed.feed(":vs<CR>");
    click(ed, 70, 1);
    click(ed, 2, 0);
    CHECK_EQ(ed.text(), std::string("abc\ndef"));
}

TEST(mouse_random_events_never_crash) {
    Ed ed("one two\nthree\n\n\xd9\x82\xd9\x84\xd9\x85 abc");
    ed.feed(":set bidi wrap nu<CR>");
    unsigned seed = 31;
    for (int i = 0; i < 3000; ++i) {
        seed = seed * 1103515245u + 12345u;
        const int button = static_cast<int>((seed >> 16) % 3) * 32 + static_cast<int>((seed >> 20) % 2) * 64;
        seed = seed * 1103515245u + 12345u;
        ed.e.handleKey(mouse(button, static_cast<int>((seed >> 8) % 120), static_cast<int>((seed >> 18) % 40),
                             (seed & 1) != 0));
    }
    ed.feed("<Esc><Esc>");
    CHECK(ed.e.mode() == Mode::Normal);
}