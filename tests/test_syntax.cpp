#include <string>

#include "buffer/line_buffer.h"
#include "check.h"
#include "syntax/syntax.h"

using namespace qalam;

namespace {
    LineBuffer make(const std::string &text) {
        LineBuffer b;
        b.replace({0, 0}, {0, 0}, text);
        return b;
    }

    Kind at(const Syntax &s, const LineBuffer &b, std::size_t row, std::size_t col) {
        const std::vector<Kind> &kinds = s.kinds(b, row);
        return col < kinds.size() ? kinds[col] : Kind::Normal;
    }
}

TEST(syntax_detects_by_extension) {
    Syntax s;
    s.setFile("main.cpp");
    CHECK_EQ(s.languageName(), std::string("cpp"));
    s.setFile("tool.PY");
    CHECK_EQ(s.languageName(), std::string("python"));
    s.setFile("notes.txt");
    CHECK_EQ(s.languageName(), std::string("none"));
    CHECK(!s.active());
}

TEST(syntax_detects_by_shebang) {
    Syntax s;
    s.setFile("script");
    s.sniff("#!/usr/bin/env python3");
    CHECK_EQ(s.languageName(), std::string("python"));

    Syntax t;
    t.setFile("run");
    t.sniff("#!/bin/bash");
    CHECK_EQ(t.languageName(), std::string("sh"));
}

TEST(syntax_extension_beats_shebang) {
    Syntax s;
    s.setFile("x.lua");
    s.sniff("#!/bin/bash");
    CHECK_EQ(s.languageName(), std::string("lua"));
}

TEST(syntax_set_language_override) {
    Syntax s;
    s.setFile("x.txt");
    CHECK(s.setLanguage("python"));
    CHECK_EQ(s.languageName(), std::string("python"));
    CHECK(s.setLanguage("none"));
    CHECK(!s.active());
    CHECK(!s.setLanguage("klingon"));
    CHECK(s.knows("rust"));
    CHECK(!s.knows("klingon"));
}

TEST(syntax_cpp_basics) {
    Syntax s;
    s.setFile("a.cpp");
    const LineBuffer b = make("int x = 42; // hi\n#include <vector>\nconst char* t = \"s\";");
    CHECK(at(s, b, 0, 0) == Kind::Type);
    CHECK(at(s, b, 0, 4) == Kind::Normal);
    CHECK(at(s, b, 0, 8) == Kind::Number);
    CHECK(at(s, b, 0, 13) == Kind::Comment);
    CHECK(at(s, b, 1, 0) == Kind::Preproc);
    CHECK(at(s, b, 1, 10) == Kind::String);
    CHECK(at(s, b, 2, 0) == Kind::Keyword);
    CHECK(at(s, b, 2, 17) == Kind::String);
}

TEST(syntax_cpp_block_comment_spans_lines) {
    Syntax s;
    s.setFile("a.cpp");
    const LineBuffer b = make("/* one\ntwo */ int x;");
    CHECK(at(s, b, 0, 3) == Kind::Comment);
    CHECK(at(s, b, 1, 1) == Kind::Comment);
    CHECK(at(s, b, 1, 5) == Kind::Comment);
    CHECK(at(s, b, 1, 6) == Kind::Normal);
    CHECK(at(s, b, 1, 8) == Kind::Type);
}
TEST(syntax_cpp_raw_string_spans_lines) {
    Syntax s;
    s.setFile("a.cpp");
    const LineBuffer b = make("auto r = R\"x(a\nb)x\"; int y;");
    CHECK(at(s, b, 0, 10) == Kind::String);
    CHECK(at(s, b, 1, 0) == Kind::String);
    CHECK(at(s, b, 1, 6) == Kind::Type);
}

TEST(syntax_python) {
    Syntax s;
    s.setFile("a.py");
    const LineBuffer b = make("def f(x):\n    \"\"\"doc\n    more\"\"\"\n    return 1  # c\n@dec");
    CHECK(at(s, b, 0, 0) == Kind::Keyword);
    CHECK(at(s, b, 1, 6) == Kind::String);
    CHECK(at(s, b, 2, 2) == Kind::String);
    CHECK(at(s, b, 3, 4) == Kind::Keyword);
    CHECK(at(s, b, 3, 11) == Kind::Number);
    CHECK(at(s, b, 3, 15) == Kind::Comment);
    CHECK(at(s, b, 4, 1) == Kind::Preproc);
}

TEST(syntax_shell_variables_and_comments) {
    Syntax s;
    s.setFile("a.sh");
    const LineBuffer b = make("echo $HOME ${PATH} $#\n# c\necho a#b");
    CHECK(at(s, b, 0, 5) == Kind::Type);
    CHECK(at(s, b, 0, 12) == Kind::Type);
    CHECK(at(s, b, 0, 20) == Kind::Type);
    CHECK(at(s, b, 1, 0) == Kind::Comment);
    CHECK(at(s, b, 2, 7) == Kind::Normal);
}

TEST(syntax_rust_lifetime_is_not_a_string) {
    Syntax s;
    s.setFile("a.rs");
    const LineBuffer b = make("fn f<'a>(c: char) { let x = 'x'; }");
    CHECK(at(s, b, 0, 5) == Kind::Normal);
    CHECK(at(s, b, 0, 28) == Kind::String);
}

TEST(syntax_javascript_template_spans_lines) {
    Syntax s;
    s.setFile("a.js");
    const LineBuffer b = make("const a = `x\ny`; let z");
    CHECK(at(s, b, 0, 11) == Kind::String);
    CHECK(at(s, b, 1, 0) == Kind::String);
    CHECK(at(s, b, 1, 4) == Kind::Keyword);
}

TEST(syntax_lua_comments) {
    Syntax s;
    s.setFile("a.lua");
    const LineBuffer b = make("local x = 1 -- c\n--[[ block\nstill ]] end");
    CHECK(at(s, b, 0, 0) == Kind::Keyword);
    CHECK(at(s, b, 0, 13) == Kind::Comment);
    CHECK(at(s, b, 1, 3) == Kind::Comment);
    CHECK(at(s, b, 2, 2) == Kind::Comment);
    CHECK(at(s, b, 2, 9) == Kind::Keyword);
}

TEST(syntax_markdown) {
    Syntax s;
    s.setFile("a.md");
    const LineBuffer b = make("# Title\n- a `c` **b**\n```\ncode\n```\n> q");
    CHECK(at(s, b, 0, 0) == Kind::Keyword);
    CHECK(at(s, b, 1, 0) == Kind::Number);
    CHECK(at(s, b, 1, 4) == Kind::String);
    CHECK(at(s, b, 1, 10) == Kind::Type);
    CHECK(at(s, b, 3, 1) == Kind::String);
    CHECK(at(s, b, 5, 0) == Kind::Comment);
}

TEST(syntax_invalidate_recomputes_state) {
    Syntax s;
    s.setFile("a.cpp");
    LineBuffer b = make("/* c\nint x;");
    CHECK(at(s, b, 1, 0) == Kind::Comment);
    b.replace({0, 0}, {0, 2}, "");
    s.invalidate(0);
    CHECK(at(s, b, 1, 0) == Kind::Type);
}

TEST(syntax_custom_color_and_reset) {
    Syntax s;
    CHECK(s.setColor("keyword", 9));
    CHECK_EQ(s.sgr(Kind::Keyword), std::string("\x1b[38;5;9m"));
    CHECK(!s.setColor("nonsense", 9));
    CHECK(!s.setColor("keyword", 300));
    s.resetColors();
    CHECK_EQ(s.sgr(Kind::Keyword), std::string("\x1b[38;5;176m"));
    CHECK_EQ(s.sgr(Kind::Normal), std::string(""));
}
