#include <string>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "harness.h"
#include "text/columns.h"

using namespace qalam;

TEST(arabic_letters_take_one_column_each) {
    CHECK_EQ(displayWidth("\xd9\x82\xd9\x84\xd9\x85"), 3);
}

TEST(arabic_diacritics_take_no_columns) {
    CHECK_EQ(displayWidth("\xd9\x82\xd9\x8e\xd9\x84\xd9\x8e\xd9\x85\xd9\x8f"), 3);
    CHECK_EQ(displayWidth("\xd8\xa7\xd9\xb0"), 1);
}

TEST(arabic_mixed_with_latin_and_digits) {
    CHECK_EQ(displayWidth("abc \xd9\x82\xd9\x84\xd9\x85 123"), 11);
    CHECK_EQ(displayWidth("\xd9\xa1\xd9\xa2\xd9\xa3"), 3);
}

TEST(arabic_cursor_steps_whole_characters) {
    const std::string s = "\xd9\x82\xd9\x8e\xd9\x84";
    CHECK_EQ(charLength(s, 0), std::size_t{2});
    CHECK_EQ(prevCharStart(s, s.size()), std::size_t{4});
}

TEST(arabic_editing_keeps_text_intact) {
    CHECK_EQ(Ed("\xd9\x82\xd9\x84\xd9\x85").feed("x").text(), std::string("\xd9\x84\xd9\x85"));
    CHECK_EQ(Ed("\xd9\x82\xd9\x84\xd9\x85").feed("$x").text(), std::string("\xd9\x82\xd9\x84"));
    CHECK_EQ(Ed("ab").feed("A\xd9\x85<Esc>").text(), std::string("ab\xd9\x85"));
}

TEST(arabic_quit_alias) {
    Ed clean("a");
    clean.feed(":خلص<CR>");
    CHECK(clean.e.shouldQuit());

    Ed dirty("a");
    dirty.feed("x:خلص<CR>");
    CHECK(!dirty.e.shouldQuit());
    dirty.feed(":خلص!<CR>");
    CHECK(dirty.e.shouldQuit());
}

TEST(arabic_save_aliases) {
    const std::string path = (std::filesystem::temp_directory_path() / "qalam_ar_save").string();
    std::filesystem::remove(path);

    Ed ed("hello");
    ed.feed(":w " + path + "<CR>");
    ed.feed("x:احفظ<CR>");
    std::ifstream in(path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK_EQ(content, std::string("ello\n"));

    ed.feed("x:احفظ_وخلص<CR>");
    CHECK(ed.e.shouldQuit());
    std::filesystem::remove(path);
}

TEST(arabic_open_alias) {
    const std::string path = (std::filesystem::temp_directory_path() / "qalam_ar_open").string();
    {
        std::ofstream out(path);
        out << "opened\n";
    }
    Ed ed("");
    ed.feed(":افتح " + path + "<CR>");
    CHECK_EQ(ed.text(), std::string("opened"));
    std::filesystem::remove(path);
}

TEST(arabic_quit_all_alias) {
    Ed ed("a");
    ed.feed("x:اخرج<CR>");
    CHECK(!ed.e.shouldQuit());
    ed.feed(":اخرج!<CR>");
    CHECK(ed.e.shouldQuit());
}

TEST(user_alias_overrides_the_builtin) {
    Ed ed("a");
    ed.feed("x:خلص<CR>");
    CHECK(!ed.e.shouldQuit());
}

TEST(unknown_arabic_command_is_reported) {
    Ed ed("");
    ed.feed(":مجهول<CR>");
    CHECK(ed.e.message().find("Not an editor command") != std::string::npos);
}