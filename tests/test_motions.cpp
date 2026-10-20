#include <string>

#include "buffer/line_buffer.h"
#include "check.h"
#include "motions/motions.h"
#include "motions/objects.h"

using namespace qalam;

namespace {
    LineBuffer make(const std::string &text) {
        LineBuffer b;
        b.replace({0, 0}, {0, 0}, text);
        return b;
    }
}

TEST(motion_word_forward_and_back) {
    const LineBuffer b = make("foo bar baz");
    CHECK_EQ(wordForward(b, {0, 0}, 1).target.col, std::size_t{4});
    CHECK_EQ(wordForward(b, {0, 0}, 2).target.col, std::size_t{8});
    CHECK_EQ(wordBackward(b, {0, 8}, 1).target.col, std::size_t{4});
    CHECK_EQ(wordBackward(b, {0, 4}, 1).target.col, std::size_t{0});
}

TEST(motion_word_end) {
    const LineBuffer b = make("foo bar");
    CHECK_EQ(wordEnd(b, {0, 0}, 1).target.col, std::size_t{2});
    CHECK_EQ(wordEnd(b, {0, 2}, 1).target.col, std::size_t{6});
}

TEST(motion_word_punctuation_is_separate) {
    const LineBuffer b = make("foo.bar");
    CHECK_EQ(wordForward(b, {0, 0}, 1).target.col, std::size_t{3});
}

TEST(motion_word_crosses_lines) {
    const LineBuffer b = make("foo\nbar");
    const Motion m = wordForward(b, {0, 0}, 1);
    CHECK_EQ(m.target.row, std::size_t{1});
    CHECK_EQ(m.target.col, std::size_t{0});
}

TEST(motion_first_non_blank) {
    const LineBuffer b = make("   x");
    CHECK_EQ(firstNonBlank(b, {0, 0}).target.col, std::size_t{3});
}

TEST(motion_paragraphs) {
    const LineBuffer b = make("a\nb\n\nc\nd");
    CHECK_EQ(paragraphForward(b, {0, 0}, 1).target.row, std::size_t{2});
    CHECK_EQ(paragraphBackward(b, {4, 0}, 1).target.row, std::size_t{2});
}

TEST(motion_match_bracket) {
    const LineBuffer b = make("(a[b])");
    const auto open = matchBracket(b, {0, 0});
    CHECK(open.has_value());
    CHECK_EQ(open->target.col, std::size_t{5});
    const auto close = matchBracket(b, {0, 5});
    CHECK(close.has_value());
    CHECK_EQ(close->target.col, std::size_t{0});
}

TEST(motion_match_bracket_across_lines) {
    const LineBuffer b = make("{\n  x\n}");
    const auto m = matchBracket(b, {0, 0});
    CHECK(m.has_value());
    CHECK_EQ(m->target.row, std::size_t{2});
}

TEST(motion_match_bracket_none) {
    const LineBuffer b = make("(abc");
    CHECK(!matchBracket(b, {0, 0}).has_value());
}

TEST(motion_find_char) {
    const LineBuffer b = make("a,b,c");
    const auto f = findChar(b, {0, 0}, ",", true, false, 1);
    CHECK(f.has_value());
    CHECK_EQ(f->target.col, std::size_t{1});

    const auto t = findChar(b, {0, 0}, ",", true, true, 1);
    CHECK(t.has_value());
    CHECK_EQ(t->target.col, std::size_t{0});

    const auto second = findChar(b, {0, 0}, ",", true, false, 2);
    CHECK(second.has_value());
    CHECK_EQ(second->target.col, std::size_t{3});

    CHECK(!findChar(b, {0, 0}, "z", true, false, 1).has_value());
}

TEST(motion_find_char_backward) {
    const LineBuffer b = make("a,b,c");
    const auto f = findChar(b, {0, 4}, ",", false, false, 1);
    CHECK(f.has_value());
    CHECK_EQ(f->target.col, std::size_t{3});
}

TEST(object_inner_word) {
    const LineBuffer b = make("foo bar baz");
    const auto r = textObject(b, {0, 5}, 'w', false, 1);
    CHECK(r.has_value());
    CHECK_EQ(b.text(r->start, r->end), std::string("bar"));
}

TEST(object_around_word_takes_space) {
    const LineBuffer b = make("foo bar baz");
    const auto r = textObject(b, {0, 5}, 'w', true, 1);
    CHECK(r.has_value());
    CHECK_EQ(b.text(r->start, r->end), std::string("bar "));
}

TEST(object_quotes) {
    const LineBuffer b = make("say \"hello world\" now");
    const auto inner = textObject(b, {0, 8}, '"', false, 1);
    CHECK(inner.has_value());
    CHECK_EQ(b.text(inner->start, inner->end), std::string("hello world"));

    const auto around = textObject(b, {0, 8}, '"', true, 1);
    CHECK(around.has_value());
    CHECK_EQ(b.text(around->start, around->end), std::string("\"hello world\" "));

    const LineBuffer tail = make("say \"hi\"");
    const auto atEnd = textObject(tail, {0, 6}, '"', true, 1);
    CHECK(atEnd.has_value());
    CHECK_EQ(tail.text(atEnd->start, atEnd->end), std::string("\"hi\""));
}

TEST(object_parens) {
    const LineBuffer b = make("f(a, (b), c)");
    const auto inner = textObject(b, {0, 3}, '(', false, 1);
    CHECK(inner.has_value());
    CHECK_EQ(b.text(inner->start, inner->end), std::string("a, (b), c"));

    const auto nested = textObject(b, {0, 6}, '(', false, 1);
    CHECK(nested.has_value());
    CHECK_EQ(b.text(nested->start, nested->end), std::string("b"));

    const auto around = textObject(b, {0, 3}, '(', true, 1);
    CHECK(around.has_value());
    CHECK_EQ(b.text(around->start, around->end), std::string("(a, (b), c)"));
}

TEST(object_none_outside_brackets) {
    const LineBuffer b = make("no brackets here");
    CHECK(!textObject(b, {0, 3}, '(', false, 1).has_value());
}

TEST(object_paragraph_is_linewise) {
    const LineBuffer b = make("a\nb\n\nc");
    const auto r = textObject(b, {0, 0}, 'p', false, 1);
    CHECK(r.has_value());
    CHECK(r->linewise);
    CHECK_EQ(r->start.row, std::size_t{0});
    CHECK_EQ(r->end.row, std::size_t{1});
}
