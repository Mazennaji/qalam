#include <string>

#include "harness.h"

TEST(sort_orders_lines) {
    CHECK_EQ(Ed("c\na\nb").feed(":sort<CR>").text(), std::string("a\nb\nc"));
    CHECK_EQ(Ed("c\na\nb").feed(":sort!<CR>").text(), std::string("c\nb\na"));
}

TEST(sort_range_only_touches_the_range) {
    CHECK_EQ(Ed("z\nc\na\nb\n0").feed(":2,4sort<CR>").text(), std::string("z\na\nb\nc\n0"));
}

TEST(sort_ignore_case) {
    CHECK_EQ(Ed("b\nA\nc").feed(":sort i<CR>").text(), std::string("A\nb\nc"));
    CHECK_EQ(Ed("b\nA\nc").feed(":sort<CR>").text(), std::string("A\nb\nc"));
    CHECK_EQ(Ed("b\nB\na").feed(":sort i<CR>").text(), std::string("a\nb\nB"));
}

TEST(sort_numeric_uses_the_first_number) {
    CHECK_EQ(Ed("x10\nx9\nx100\nnone").feed(":sort n<CR>").text(),
             std::string("none\nx9\nx10\nx100"));
    CHECK_EQ(Ed("a-5\nb3\nc-10").feed(":sort n<CR>").text(), std::string("c-10\na-5\nb3"));
    CHECK_EQ(Ed("x10\nx9\nx100").feed(":sort! n<CR>").text(), std::string("x100\nx10\nx9"));
}

TEST(sort_unique_removes_duplicates) {
    Ed ed("b\na\nb\na\nc");
    ed.feed(":sort u<CR>");
    CHECK_EQ(ed.text(), std::string("a\nb\nc"));
    CHECK(ed.e.message().find("2 removed") != std::string::npos);
}

TEST(sort_is_stable_for_equal_keys) {
    CHECK_EQ(Ed("b2\na2\nc1").feed(":sort n<CR>").text(), std::string("c1\nb2\na2"));
}

TEST(sort_is_one_undo_step) {
    Ed ed("c\na\nb");
    ed.feed(":sort<CR>u");
    CHECK_EQ(ed.text(), std::string("c\na\nb"));
}

TEST(sort_reports_already_sorted_and_bad_flags) {
    Ed ed("a\nb");
    ed.feed(":sort<CR>");
    CHECK(ed.e.message().find("Already sorted") != std::string::npos);
    ed.feed(":sort q<CR>");
    CHECK(ed.e.message().find("usage") != std::string::npos);
    CHECK_EQ(ed.text(), std::string("a\nb"));
}

TEST(sort_single_line_and_empty_lines) {
    CHECK_EQ(Ed("only").feed(":sort<CR>").text(), std::string("only"));
    CHECK_EQ(Ed("b\n\na").feed(":sort<CR>").text(), std::string("\na\nb"));
}