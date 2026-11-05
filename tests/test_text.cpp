#include <string>

#include "check.h"
#include "text/columns.h"

using namespace qalam;

TEST(utf8_char_length) {
    const std::string s = "a\xc3\xa9\xe6\x97\xa5";
    CHECK_EQ(charLength(s, 0), std::size_t{1});
    CHECK_EQ(charLength(s, 1), std::size_t{2});
    CHECK_EQ(charLength(s, 3), std::size_t{3});
}

TEST(utf8_prev_char_start) {
    const std::string s = "a\xc3\xa9";
    CHECK_EQ(prevCharStart(s, 3), std::size_t{1});
    CHECK_EQ(prevCharStart(s, 1), std::size_t{0});
}

TEST(utf8_snap_to_start) {
    const std::string s = "a\xc3\xa9";
    CHECK_EQ(snapToStart(s, 2), std::size_t{1});
    CHECK_EQ(snapToStart(s, 1), std::size_t{1});
}

TEST(width_ascii_and_wide) {
    CHECK_EQ(displayWidth("hello"), 5);
    CHECK_EQ(displayWidth("\xe6\x97\xa5\xe6\x9c\xac"), 4);
}

TEST(width_combining_mark_is_zero) {
    CHECK_EQ(displayWidth("e\xcc\x81"), 1);
}

TEST(tab_expands_to_stop) {
    setTabStop(4);
    CHECK_EQ(displayColumn("\tx", 1), 4);
    CHECK_EQ(displayColumn("ab\tx", 3), 4);
    setTabStop(8);
    CHECK_EQ(displayColumn("\tx", 1), 8);
    setTabStop(4);
}

TEST(tab_stop_is_clamped) {
    setTabStop(0);
    CHECK_EQ(tabStop(), 1);
    setTabStop(99);
    CHECK_EQ(tabStop(), 32);
    setTabStop(4);
}

TEST(byte_for_column_round_trip) {
    const std::string s = "\xe6\x97\xa5\xe6\x9c\xac" "ab";
    CHECK_EQ(byteForColumn(s, 0), std::size_t{0});
    CHECK_EQ(byteForColumn(s, 2), std::size_t{3});
    CHECK_EQ(byteForColumn(s, 4), std::size_t{6});
}

TEST(truncate_never_splits_wide_char) {
    CHECK_EQ(truncateToWidth("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", 4),
             std::string("\xe6\x97\xa5\xe6\x9c\xac"));
    CHECK_EQ(truncateToWidth("\xe6\x97\xa5\xe6\x9c\xac", 3), std::string("\xe6\x97\xa5"));
}

TEST(pop_last_char_removes_whole_char) {
    std::string s = "a\xc3\xa9";
    popLastChar(s);
    CHECK_EQ(s, std::string("a"));
}

TEST(wrap_height_counts_rows) {
    CHECK_EQ(wrapHeight("", 10), 1);
    CHECK_EQ(wrapHeight("0123456789", 10), 1);
    CHECK_EQ(wrapHeight("01234567890", 10), 2);
    CHECK_EQ(wrapHeight(std::string(25, 'x'), 10), 3);
}
