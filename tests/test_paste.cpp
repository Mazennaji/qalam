#include <string>

#include "harness.h"
#include "terminal/decoder.h"

using namespace qalam;

namespace {
    Key paste(const std::string &text) {
        Key key;
        key.type = KeyType::Paste;
        key.text = text;
        return key;
    }
}

TEST(decode_paste_complete) {
    const std::string bytes = "\x1b[200~hi\x1b[201~";
    const Decoded d = decodeKey(bytes, false);
    CHECK(!d.needMore);
    CHECK(d.key.type == KeyType::Paste);
    CHECK_EQ(d.key.text, std::string("hi"));
    CHECK_EQ(d.used, bytes.size());
}

TEST(decode_paste_keeps_escapes_newlines_and_utf8) {
    const std::string body = "a\x1b[A\nb\xc3\xa9\r\n";
    const Decoded d = decodeKey("\x1b[200~" + body + "\x1b[201~", false);
    CHECK(d.key.type == KeyType::Paste);
    CHECK_EQ(d.key.text, body);
}

TEST(decode_paste_waits_for_the_end_marker) {
    CHECK(decodeKey("\x1b[200~hi", false).needMore);
    CHECK(decodeKey("\x1b[200", false).needMore);

    const Decoded cut = decodeKey("\x1b[200~hi", true);
    CHECK(cut.key.type == KeyType::Paste);
    CHECK_EQ(cut.key.text, std::string("hi"));
}

TEST(decode_paste_leaves_following_keys_alone) {
    const std::string bytes = "\x1b[200~x\x1b[201~y";
    const Decoded d = decodeKey(bytes, false);
    CHECK_EQ(d.used, bytes.size() - 1);
}

TEST(decode_stray_paste_end_is_dropped) {
    const Decoded d = decodeKey("\x1b[201~", false);
    CHECK(d.key.type == KeyType::None);
    CHECK_EQ(d.used, std::size_t{6});
}

TEST(paste_in_insert_mode_is_one_undo_step) {
    Ed ed("");
    ed.feed("i");
    ed.e.handleKey(paste("one\ntwo\nthree"));
    ed.feed("<Esc>");
    CHECK_EQ(ed.text(), std::string("one\ntwo\nthree"));
    ed.feed("u");
    CHECK_EQ(ed.text(), std::string(""));
}

TEST(paste_cursor_lands_after_the_text) {
    Ed ed("");
    ed.feed("i");
    ed.e.handleKey(paste("ab\ncd"));
    CHECK_EQ(ed.row(), std::size_t{1});
    CHECK_EQ(ed.col(), std::size_t{2});
    ed.feed("X<Esc>");
    CHECK_EQ(ed.text(), std::string("ab\ncdX"));
}

TEST(paste_skips_autoindent) {
    Ed ed("\tfoo");
    ed.feed(":set ai<CR>A");
    ed.e.handleKey(paste("\nx\n  y"));
    ed.feed("<Esc>");
    CHECK_EQ(ed.text(), std::string("\tfoo\nx\n  y"));
}

TEST(paste_ignores_mappings) {
    Ed ed("");
    ed.feed(":map insert jk <Lt>Esc><CR>i");
    ed.e.handleKey(paste("jk"));
    CHECK(ed.e.mode() == Mode::Insert);
    CHECK_EQ(ed.text(), std::string("jk"));
}

TEST(paste_normalizes_crlf) {
    Ed ed("");
    ed.feed("i");
    ed.e.handleKey(paste("a\r\nb\rc"));
    CHECK_EQ(ed.text(), std::string("a\nb\nc"));
}

TEST(paste_into_command_line_flattens_newlines) {
    Ed ed("");
    ed.feed(":");
    ed.e.handleKey(paste("set\nnu"));
    CHECK(ed.e.mode() == Mode::Command);
    ed.feed("<CR>");
    CHECK(ed.e.message().find("Unknown option") == std::string::npos);
}

TEST(paste_in_normal_mode_changes_nothing) {
    Ed ed("abc");
    ed.e.handleKey(paste("zzz"));
    CHECK_EQ(ed.text(), std::string("abc"));
    CHECK(ed.e.message().find("Paste ignored") != std::string::npos);
}

TEST(paste_large_block) {
    Ed ed("");
    ed.feed("i");
    std::string big;
    for (int i = 0; i < 20000; ++i) big += "line " + std::to_string(i) + "\n";
    big += "end";
    ed.e.handleKey(paste(big));
    CHECK_EQ(ed.e.cursor().row, std::size_t{20000});
    CHECK_EQ(ed.text().size(), big.size());
}
