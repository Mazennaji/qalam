#include <string>

#include "check.h"
#include "search/regex.h"
#include "search/search.h"
#include "search/substitute.h"

using namespace qalam;

namespace {
    std::optional<Match> find(const std::string &pattern, const std::string &text,
                              bool fold = false, std::size_t from = 0) {
        std::string error;
        const std::optional<Regex> re = Regex::compile(pattern, fold, error);
        if (!re) return std::nullopt;
        return re->search(text, from);
    }
}

TEST(regex_literal) {
    const auto m = find("bar", "foo bar baz");
    CHECK(m.has_value());
    CHECK_EQ(m->begin, std::size_t{4});
    CHECK_EQ(m->end, std::size_t{7});
}

TEST(regex_no_match) {
    CHECK(!find("xyz", "foo bar").has_value());
}

TEST(regex_dot_and_repeats) {
    CHECK_EQ(find("a.c", "xabcx")->begin, std::size_t{1});
    CHECK_EQ(find("ab*c", "ac")->end, std::size_t{2});
    CHECK_EQ(find("ab+c", "abbbc")->end, std::size_t{5});
    CHECK(!find("ab+c", "ac").has_value());
    CHECK_EQ(find("colou?r", "color")->end, std::size_t{5});
}

TEST(regex_greedy) {
    const auto m = find("a.*b", "xaxxbxxb");
    CHECK_EQ(m->begin, std::size_t{1});
    CHECK_EQ(m->end, std::size_t{8});
}

TEST(regex_anchors) {
    CHECK(find("^foo", "foo bar").has_value());
    CHECK(!find("^bar", "foo bar").has_value());
    CHECK(find("bar$", "foo bar").has_value());
    CHECK(!find("foo$", "foo bar").has_value());
    CHECK(find("^$", "").has_value());
}

TEST(regex_word_boundaries) {
    const auto m = find("\\<foo\\>", "foobar foo");
    CHECK(m.has_value());
    CHECK_EQ(m->begin, std::size_t{7});
    CHECK(!find("\\<foo\\>", "foobar").has_value());
}

TEST(regex_classes) {
    CHECK_EQ(find("[0-9]+", "abc 123 def")->begin, std::size_t{4});
    CHECK_EQ(find("[0-9]+", "abc 123 def")->end, std::size_t{7});
    CHECK_EQ(find("[^a-c]", "abcd")->begin, std::size_t{3});
    CHECK_EQ(find("\\d\\d", "a12")->begin, std::size_t{1});
    CHECK_EQ(find("\\s+", "a  b")->end, std::size_t{3});
    CHECK_EQ(find("\\w+", "  hello_1 ")->end, std::size_t{9});
    CHECK_EQ(find("\\W", "ab c")->begin, std::size_t{2});
}

TEST(regex_alternation_and_groups) {
    const auto m = find("(foo|bar)baz", "xbarbaz");
    CHECK(m.has_value());
    CHECK_EQ(m->begin, std::size_t{1});
    CHECK_EQ(m->groups[1].first, std::size_t{1});
    CHECK_EQ(m->groups[1].second, std::size_t{4});
    CHECK(find("cat|dog", "hotdog").has_value());
}

TEST(regex_group_repeat) {
    CHECK_EQ(find("(ab)+", "xababab")->end, std::size_t{7});
}

TEST(regex_escapes) {
    CHECK_EQ(find("a\\.b", "axb a.b")->begin, std::size_t{4});
    CHECK(find("\\(x\\)", "(x)").has_value());
    CHECK(find("a\\tb", "a\tb").has_value());
}

TEST(regex_ignore_case) {
    CHECK(find("foo", "FOO", true).has_value());
    CHECK(!find("foo", "FOO", false).has_value());
    CHECK(find("[a-c]+", "ABC", true).has_value());
}

TEST(regex_utf8_dot_is_one_char) {
    const auto m = find("h.llo", "h\xc3\xa9llo");
    CHECK(m.has_value());
    CHECK_EQ(m->end, std::size_t{6});
}

TEST(regex_zero_width_match) {
    const auto m = find("x*", "abc");
    CHECK(m.has_value());
    CHECK_EQ(m->begin, std::size_t{0});
    CHECK_EQ(m->end, std::size_t{0});
}

TEST(regex_errors) {
    std::string error;
    CHECK(!Regex::compile("(a", false, error).has_value());
    CHECK(error.find("missing )") != std::string::npos);

    error.clear();
    CHECK(!Regex::compile("*a", false, error).has_value());
    CHECK(error.find("nothing to repeat") != std::string::npos);

    error.clear();
    CHECK(!Regex::compile("[a", false, error).has_value());
    CHECK(error.find("missing ]") != std::string::npos);

    error.clear();
    CHECK(!Regex::compile("a)", false, error).has_value());
}

TEST(regex_catastrophic_pattern_terminates) {
    CHECK(!find("(a*)*b", std::string(40, 'a')).has_value());
}

TEST(search_matches_all_in_line) {
    const std::vector<Span> spans = findInLine("foo bar foo", "foo");
    CHECK_EQ(spans.size(), std::size_t{2});
    CHECK_EQ(spans[1].first, std::size_t{8});
}

TEST(search_smartcase) {
    CHECK_EQ(findInLine("Foo foo", "foo").size(), std::size_t{2});
    CHECK_EQ(findInLine("Foo foo", "Foo").size(), std::size_t{1});
    CHECK_EQ(findInLine("a1 b2", "\\w\\d").size(), std::size_t{2});
}

TEST(search_validate_pattern) {
    std::string error;
    CHECK(validatePattern("a+b", error));
    CHECK(!validatePattern("(", error));
    CHECK(validatePattern("", error));
}

TEST(substitute_basic) {
    CHECK_EQ(substituteLine("a a a", "a", "b", false, CaseMode::Smart).line, std::string("b a a"));
    CHECK_EQ(substituteLine("a a a", "a", "b", true, CaseMode::Smart).line, std::string("b b b"));
    CHECK_EQ(substituteLine("a a a", "a", "b", true, CaseMode::Smart).count, std::size_t{3});
}

TEST(substitute_groups_and_ampersand) {
    CHECK_EQ(substituteLine("user@host", "(\\w+)@(\\w+)", "\\2 at \\1", false, CaseMode::Smart).line,
             std::string("host at user"));
    CHECK_EQ(substituteLine("x 12 y", "\\d+", "[&]", false, CaseMode::Smart).line,
             std::string("x [12] y"));
    CHECK_EQ(substituteLine("x 12", "\\d+", "\\&", false, CaseMode::Smart).line, std::string("x &"));
}

TEST(substitute_anchors) {
    CHECK_EQ(substituteLine("text", "^", "# ", false, CaseMode::Smart).line, std::string("# text"));
    CHECK_EQ(substituteLine("text   ", "\\s+$", "", false, CaseMode::Smart).line, std::string("text"));
}

TEST(substitute_empty_matches_global) {
    CHECK_EQ(substituteLine("abc", "x*", "-", true, CaseMode::Smart).line, std::string("-a-b-c-"));
}

TEST(substitute_no_match_returns_original) {
    const SubstituteResult r = substituteLine("abc", "z", "y", true, CaseMode::Smart);
    CHECK_EQ(r.line, std::string("abc"));
    CHECK_EQ(r.count, std::size_t{0});
}

TEST(substitute_case_modes) {
    CHECK_EQ(substituteLine("Foo", "foo", "x", false, CaseMode::Sensitive).count, std::size_t{0});
    CHECK_EQ(substituteLine("Foo", "foo", "x", false, CaseMode::Insensitive).count, std::size_t{1});
}
