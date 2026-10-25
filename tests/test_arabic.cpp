#include <string>

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
