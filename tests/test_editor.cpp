#include <string>

#include "harness.h"

using qalam::Mode;

TEST(ed_dd_and_counts) {
    CHECK_EQ(Ed("a\nb\nc").feed("dd").text(), std::string("b\nc"));
    CHECK_EQ(Ed("1\n2\n3\n4\n5").feed("3dd").text(), std::string("4\n5"));
    CHECK_EQ(Ed("a\nb\nc").feed("Gdd").text(), std::string("a\nb"));
}

TEST(ed_delete_word_and_chars) {
    CHECK_EQ(Ed("foo bar").feed("dw").text(), std::string("bar"));
    CHECK_EQ(Ed("a b c d").feed("d2w").text(), std::string("c d"));
    CHECK_EQ(Ed("abc").feed("x").text(), std::string("bc"));
    CHECK_EQ(Ed("abc").feed("2x").text(), std::string("c"));
    CHECK_EQ(Ed("abc").feed("$x").text(), std::string("ab"));
}

TEST(ed_change_commands) {
    CHECK_EQ(Ed("foo bar baz").feed("wciwXX<Esc>").text(), std::string("foo XX baz"));
    CHECK_EQ(Ed("foo bar").feed("cwX<Esc>").text(), std::string("X bar"));
    CHECK_EQ(Ed("foo bar").feed("C!<Esc>").text(), std::string("!"));
    CHECK_EQ(Ed("one\ntwo").feed("ccnew<Esc>").text(), std::string("new\ntwo"));
}

TEST(ed_text_objects) {
    CHECK_EQ(Ed("say \"hello\" now").feed("fhci\"X<Esc>").text(),
             std::string("say \"X\" now"));
    CHECK_EQ(Ed("f(a, b)").feed("fadi(").text(), std::string("f()"));
    CHECK_EQ(Ed("foo bar baz").feed("wdaw").text(), std::string("foo baz"));
}

TEST(ed_insert_and_open_lines) {
    CHECK_EQ(Ed("a").feed("ob<Esc>").text(), std::string("a\nb"));
    CHECK_EQ(Ed("a").feed("Ob<Esc>").text(), std::string("b\na"));
    CHECK_EQ(Ed("bc").feed("ia<Esc>").text(), std::string("abc"));
    CHECK_EQ(Ed("ac").feed("ab<Esc>").text(), std::string("abc"));
    CHECK_EQ(Ed("b").feed("Ic<Esc>").text(), std::string("cb"));
}

TEST(ed_backspace_joins_lines) {
    CHECK_EQ(Ed("a\nb").feed("jI<BS><Esc>").text(), std::string("ab"));
}

TEST(ed_modes) {
    Ed ed("abc");
    CHECK(ed.e.mode() == Mode::Normal);
    ed.feed("i");
    CHECK(ed.e.mode() == Mode::Insert);
    ed.feed("<Esc>v");
    CHECK(ed.e.mode() == Mode::Visual);
    ed.feed("<Esc>:");
    CHECK(ed.e.mode() == Mode::Command);
    ed.feed("<Esc>/");
    CHECK(ed.e.mode() == Mode::Search);
    ed.feed("<Esc>");
    CHECK(ed.e.mode() == Mode::Normal);
}

TEST(ed_cursor_clamps_after_insert) {
    Ed ed("abc");
    ed.feed("A<Esc>");
    CHECK_EQ(ed.col(), std::size_t{2});
}

TEST(ed_motions_move_cursor) {
    Ed ed("1\n2\n3\n4");
    ed.feed("G");
    CHECK_EQ(ed.row(), std::size_t{3});
    ed.feed("gg");
    CHECK_EQ(ed.row(), std::size_t{0});
    ed.feed("3G");
    CHECK_EQ(ed.row(), std::size_t{2});
    ed.feed("k");
    CHECK_EQ(ed.row(), std::size_t{1});

    Ed words("foo bar baz");
    words.feed("2w");
    CHECK_EQ(words.col(), std::size_t{8});
    words.feed("b");
    CHECK_EQ(words.col(), std::size_t{4});
    words.feed("$");
    CHECK_EQ(words.col(), std::size_t{10});
    words.feed("0");
    CHECK_EQ(words.col(), std::size_t{0});
}

TEST(ed_percent_matches_bracket) {
    Ed ed("(a)");
    ed.feed("%");
    CHECK_EQ(ed.col(), std::size_t{2});
}

TEST(ed_undo_and_redo) {
    CHECK_EQ(Ed("abc").feed("xxu").text(), std::string("bc"));
    CHECK_EQ(Ed("abc").feed("xxx2u").text(), std::string("bc"));
    CHECK_EQ(Ed("abc").feed("xu<C-r>").text(), std::string("bc"));
    CHECK_EQ(Ed("abc").feed("ione<Esc>u").text(), std::string("abc"));
    CHECK_EQ(Ed("abc").feed("u").text(), std::string("abc"));
}

TEST(ed_dot_repeat) {
    CHECK_EQ(Ed("a a a").feed("dw.").text(), std::string("a"));
    CHECK_EQ(Ed("abcdef").feed("x.").text(), std::string("cdef"));
    CHECK_EQ(Ed("abc").feed("Ahi<Esc>.").text(), std::string("abchihi"));
    CHECK_EQ(Ed("a\nb\nc").feed("ddj.").text(), std::string("b"));
}

TEST(ed_macros) {
    CHECK_EQ(Ed("a\nb\nc").feed("qaAx<Esc>jq2@a").text(), std::string("ax\nbx\ncx"));
    CHECK_EQ(Ed("a\nb\nc").feed("qaAx<Esc>jq@a@@").text(), std::string("ax\nbx\ncx"));
}

TEST(ed_yank_and_put) {
    CHECK_EQ(Ed("a\nb").feed("yyp").text(), std::string("a\na\nb"));
    CHECK_EQ(Ed("a\nb").feed("yyP").text(), std::string("a\na\nb"));
    CHECK_EQ(Ed("ab").feed("xp").text(), std::string("ba"));
    CHECK_EQ(Ed("foo bar").feed("ywwP").text(), std::string("foo foo bar"));
    CHECK_EQ(Ed("a").feed("yy3p").text(), std::string("a\na\na\na"));
}

TEST(ed_named_registers) {
    CHECK_EQ(Ed("one\ntwo").feed("\"ayyj\"ap").text(), std::string("one\ntwo\none"));
    CHECK_EQ(Ed("one\ntwo").feed("\"ayyj\"Ayy\"ap").text(),
             std::string("one\ntwo\none\ntwo"));
}

TEST(ed_numbered_registers) {
    CHECK_EQ(Ed("a\nb\nc").feed("dddd\"2p").text(), std::string("c\na"));
    CHECK_EQ(Ed("a\nb\nc").feed("dddd\"1p").text(), std::string("c\nb"));
    CHECK_EQ(Ed("a\nb").feed("yyjdd\"0p").text(), std::string("a\na"));
}

TEST(ed_small_delete_and_black_hole) {
    CHECK_EQ(Ed("foo bar").feed("dw\"-P").text(), std::string("foo bar"));
    CHECK_EQ(Ed("a\nb").feed("yyj\"_ddp").text(), std::string("a\na"));
}

TEST(ed_join_case_replace) {
    CHECK_EQ(Ed("a\n  b").feed("J").text(), std::string("a b"));
    CHECK_EQ(Ed("abC").feed("3~").text(), std::string("ABc"));
    CHECK_EQ(Ed("abc").feed("rx").text(), std::string("xbc"));
    CHECK_EQ(Ed("abc").feed("3rx").text(), std::string("xxx"));
}

TEST(ed_search_motions) {
    Ed ed("foo\nbar\nfoo");
    ed.feed("/bar<CR>");
    CHECK_EQ(ed.row(), std::size_t{1});

    Ed again("foo\nbar\nfoo");
    again.feed("/foo<CR>");
    CHECK_EQ(again.row(), std::size_t{2});
    again.feed("n");
    CHECK_EQ(again.row(), std::size_t{0});
    again.feed("N");
    CHECK_EQ(again.row(), std::size_t{2});

    Ed star("foo bar\nfoo");
    star.feed("*");
    CHECK_EQ(star.row(), std::size_t{1});
}

TEST(ed_delete_with_search_motion) {
    CHECK_EQ(Ed("abc def ghi").feed("d/ghi<CR>").text(), std::string("ghi"));
}

TEST(ed_marks) {
    CHECK_EQ(Ed("a\nb\nc").feed("majd'a").text(), std::string("c"));
}

TEST(ed_substitute) {
    CHECK_EQ(Ed("a a").feed(":s/a/b/<CR>").text(), std::string("b a"));
    CHECK_EQ(Ed("a a").feed(":s/a/b/g<CR>").text(), std::string("b b"));
    CHECK_EQ(Ed("a\na").feed(":%s/a/b/<CR>").text(), std::string("b\nb"));
    CHECK_EQ(Ed("a\na\na").feed(":2,3s/a/b/<CR>").text(), std::string("a\nb\nb"));
    CHECK_EQ(Ed("john smith").feed(":s/(\\w+) (\\w+)/\\2 \\1/<CR>").text(),
             std::string("smith john"));
    CHECK_EQ(Ed("a\nb").feed(":%s/^/# /<CR>").text(), std::string("# a\n# b"));
}

TEST(ed_substitute_is_one_undo_step) {
    Ed ed("a\na\na");
    ed.feed(":%s/a/b/<CR>u");
    CHECK_EQ(ed.text(), std::string("a\na\na"));
}

TEST(ed_substitute_reports_errors) {
    Ed bad("abc");
    bad.feed(":s/(a/b/<CR>");
    CHECK(bad.e.message().find("Invalid pattern: missing )") != std::string::npos);
    CHECK_EQ(bad.text(), std::string("abc"));

    Ed miss("abc");
    miss.feed(":s/zzz/y/<CR>");
    CHECK(miss.e.message().find("Pattern not found") != std::string::npos);
}

TEST(ed_global_delete_and_invert) {
    CHECK_EQ(Ed("a1\nb\na2\nc").feed(":g/a/d<CR>").text(), std::string("b\nc"));
    CHECK_EQ(Ed("a1\nb\na2\nc").feed(":v/a/d<CR>").text(), std::string("a1\na2"));
    CHECK_EQ(Ed("a\n\nb\n\nc").feed(":g/^$/d<CR>").text(), std::string("a\nb\nc"));
    CHECK_EQ(Ed("a\nb\nc").feed(":g/a\\|c/d<CR>").text().empty(), false);
}

TEST(ed_global_delete_consecutive_lines) {
    CHECK_EQ(Ed("a\na\na\nb").feed(":g/a/d<CR>").text(), std::string("b"));
}

TEST(ed_global_is_one_undo_step) {
    Ed ed("a1\nb\na2\nc");
    ed.feed(":g/a/d<CR>u");
    CHECK_EQ(ed.text(), std::string("a1\nb\na2\nc"));
}

TEST(ed_global_reverse_file) {
    CHECK_EQ(Ed("1\n2\n3").feed(":g/^/m0<CR>").text(), std::string("3\n2\n1"));
}

TEST(ed_global_substitute) {
    CHECK_EQ(Ed("ab\ncd\nab").feed(":g/ab/s/b/X/<CR>").text(), std::string("aX\ncd\naX"));
}

TEST(ed_global_count_message) {
    Ed ed("a\nb\na");
    ed.feed(":g/a/<CR>");
    CHECK(ed.e.message().find("2 matching lines") != std::string::npos);
    CHECK_EQ(ed.text(), std::string("a\nb\na"));
}

TEST(ed_range_commands) {
    CHECK_EQ(Ed("1\n2\n3").feed(":2d<CR>").text(), std::string("1\n3"));
    CHECK_EQ(Ed("1\n2\n3").feed(":1,2d<CR>").text(), std::string("3"));
    CHECK_EQ(Ed("1\n2\n3").feed(":2,3m0<CR>").text(), std::string("2\n3\n1"));
    CHECK_EQ(Ed("1\n2\n3").feed(":1m$<CR>").text(), std::string("2\n3\n1"));
    CHECK_EQ(Ed("1\n2").feed(":1t.<CR>").text(), std::string("1\n1\n2"));
    CHECK_EQ(Ed("1\n2\n3").feed(":%y<CR>Gp").text(), std::string("1\n2\n3\n1\n2\n3"));
}

TEST(ed_bare_range_jumps) {
    Ed ed("1\n2\n3\n4");
    ed.feed(":3<CR>");
    CHECK_EQ(ed.row(), std::size_t{2});
    ed.feed(":$<CR>");
    CHECK_EQ(ed.row(), std::size_t{3});
}

TEST(ed_normal_command) {
    CHECK_EQ(Ed("a\nb").feed(":%normal Ax<Lt>Esc><CR>").text(), std::string("ax\nbx"));
    CHECK_EQ(Ed("ab\ncd").feed(":%normal dl<CR>").text(), std::string("b\nd"));
}

TEST(ed_normal_command_is_one_undo_step) {
    Ed ed("a\nb\nc");
    ed.feed(":%normal Ax<Lt>Esc><CR>u");
    CHECK_EQ(ed.text(), std::string("a\nb\nc"));
}

TEST(ed_visual_modes) {
    CHECK_EQ(Ed("abcdef").feed("lvld").text(), std::string("adef"));
    CHECK_EQ(Ed("a\nb\nc").feed("Vjd").text(), std::string("c"));
    CHECK_EQ(Ed("abc").feed("vly$p").text(), std::string("abcab"));
    CHECK_EQ(Ed("abc def").feed("veU").text().empty(), false);
}

TEST(ed_visual_block) {
    CHECK_EQ(Ed("abc\nabc\nabc").feed("<C-v>jlld").text(), std::string("c\nc\nabc"));
    CHECK_EQ(Ed("ab\nab\nab").feed("<C-v>jjIX<Esc>").text(), std::string("Xab\nXab\nXab"));
    CHECK_EQ(Ed("ab\nab").feed("<C-v>jAX<Esc>").text(), std::string("aXb\naXb"));
}

TEST(ed_indent_commands) {
    CHECK_EQ(Ed("a").feed("<Gt><Gt>").text(), std::string("\ta"));
    CHECK_EQ(Ed("\ta").feed("<Lt><Lt>").text(), std::string("a"));
    CHECK_EQ(Ed("a").feed(":set et<CR><Gt><Gt>").text(), std::string("    a"));
    CHECK_EQ(Ed("a").feed(":set et sw=2<CR><Gt><Gt>").text(), std::string("  a"));
    CHECK_EQ(Ed("a\nb").feed("<Gt>j").text(), std::string("\ta\n\tb"));
}

TEST(ed_expandtab_and_autoindent) {
    CHECK_EQ(Ed("").feed(":set et<CR>i<Tab>x<Esc>").text(), std::string("    x"));
    CHECK_EQ(Ed("ab").feed(":set et<CR>la<Tab>x<Esc>").text().find('\t'), std::string::npos);
    CHECK_EQ(Ed("\tfoo").feed(":set ai<CR>A<CR>x<Esc>").text(), std::string("\tfoo\n\tx"));
    CHECK_EQ(Ed("\tfoo").feed(":set ai<CR>ox<Esc>").text(), std::string("\tfoo\n\tx"));
    CHECK_EQ(Ed("\tfoo").feed("A<CR>x<Esc>").text(), std::string("\tfoo\nx"));
}

TEST(ed_set_options_report) {
    Ed ed("");
    ed.feed(":set<CR>");
    CHECK(ed.e.message().find("ts=4") != std::string::npos);
    ed.feed(":set ts=8<CR>:set<CR>");
    CHECK(ed.e.message().find("ts=8") != std::string::npos);
    ed.feed(":set bogus<CR>");
    CHECK(ed.e.message().find("Unknown option: bogus") != std::string::npos);
    ed.feed(":set ts=0<CR>");
    CHECK(ed.e.message().find("Unknown option") != std::string::npos);
}

TEST(ed_unknown_command) {
    Ed ed("");
    ed.feed(":bogus<CR>");
    CHECK(ed.e.message().find("Not an editor command: bogus") != std::string::npos);
}

TEST(ed_quit_guards_unsaved_changes) {
    Ed clean("a");
    clean.feed(":q<CR>");
    CHECK(clean.e.shouldQuit());

    Ed dirty("a");
    dirty.feed("x:q<CR>");
    CHECK(!dirty.e.shouldQuit());
    dirty.feed(":q!<CR>");
    CHECK(dirty.e.shouldQuit());
}

TEST(ed_command_line_editing) {
    CHECK_EQ(Ed("a a").feed(":s/a/b/g<C-u>s/a/c/<CR>").text(), std::string("c a"));
    CHECK_EQ(Ed("a").feed(":s/a/bb/<Left><Left><BS><CR>").text().empty(), false);
    Ed ed("x");
    ed.feed(":set nu<CR>:<Up>");
    CHECK(ed.e.mode() == Mode::Command);
    ed.feed("<Esc>");
}

TEST(ed_command_history_recall) {
    Ed ed("a\nb\nc");
    ed.feed(":1d<CR>:<Up><CR>");
    CHECK_EQ(ed.text(), std::string("c"));
}

TEST(ed_utf8_editing) {
    CHECK_EQ(Ed("h\xc3\xa9llo").feed("lx").text(), std::string("hllo"));
    CHECK_EQ(Ed("\xe6\x97\xa5\xe6\x9c\xac").feed("$x").text(), std::string("\xe6\x97\xa5"));
    CHECK_EQ(Ed("\xe6\x97\xa5\xe6\x9c\xac").feed("lh").e.cursor().col, std::size_t{0});
    CHECK_EQ(Ed("ab").feed("A\xc3\xa9<Esc>").text(), std::string("ab\xc3\xa9"));
}

TEST(ed_find_char_motions) {
    CHECK_EQ(Ed("a,b,c").feed("f,x").text(), std::string("ab,c"));
    CHECK_EQ(Ed("a,b,c").feed("f,;x").text(), std::string("a,bc"));
    CHECK_EQ(Ed("a,b,c").feed("dt,").text(), std::string(",b,c"));
    CHECK_EQ(Ed("a,b,c").feed("$F,x").text(), std::string("a,bc"));
}

TEST(ed_paste_linewise_cursor_and_counts) {
    Ed ed("a\nb");
    ed.feed("yyjp");
    CHECK_EQ(ed.row(), std::size_t{2});
    CHECK_EQ(ed.text(), std::string("a\nb\na"));
}

TEST(ed_window_split_and_close) {
    Ed ed("a");
    ed.feed(":sp<CR>:close<CR>");
    CHECK_EQ(ed.text(), std::string("a"));
    ed.feed("<C-w>v<C-w>c");
    CHECK_EQ(ed.text(), std::string("a"));
}

TEST(ed_stress_random_keys_do_not_crash) {
    const std::string keys[] = {
        "dd", "p", "u", "<C-r>", "x", "J", "yy", "P", ".", "~", "o<Esc>",
        "O<Esc>", "G", "gg", "dw", "cwX<Esc>", "w", "b", "$", "0",
        ">>", "j", "k", "v", "<Esc>", "V", "d", "<C-v>", "qa", "q",
        "@a", "ma", "'a", "%", "*", "n", "N"
    };
    Ed ed("one two\nthree (four)\n\nfive \"six\"\n\tseven");
    unsigned seed = 12345;
    for (int i = 0; i < 4000; ++i) {
        seed = seed * 1103515245u + 12345u;
        ed.feed(keys[(seed >> 16) % (sizeof keys / sizeof keys[0])]);
        const qalam::Pos c = ed.e.cursor();
        CHECK(c.row < 100000);
    }
    ed.feed("<Esc><Esc>");
    CHECK(ed.e.mode() == Mode::Normal);
}
