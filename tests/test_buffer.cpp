#include <string>

#include "buffer/line_buffer.h"
#include "check.h"
#include "undo/history.h"

using namespace qalam;

namespace {
    LineBuffer make(const std::string &text) {
        LineBuffer b;
        b.replace({0, 0}, {0, 0}, text);
        return b;
    }

    std::string all(const LineBuffer &b) {
        return b.text({0, 0}, {b.lineCount() - 1, b.line(b.lineCount() - 1).size()});
    }
}

TEST(buffer_starts_with_one_empty_line) {
    LineBuffer b;
    CHECK_EQ(b.lineCount(), std::size_t{1});
    CHECK(b.line(0).empty());
}

TEST(buffer_insert_text_with_newlines) {
    LineBuffer b = make("one\ntwo\nthree");
    CHECK_EQ(b.lineCount(), std::size_t{3});
    CHECK_EQ(b.line(1), std::string("two"));
}

TEST(buffer_insert_in_middle_of_line) {
    LineBuffer b = make("heworld");
    b.replace({0, 2}, {0, 2}, "llo ");
    CHECK_EQ(b.line(0), std::string("hello world"));
}

TEST(buffer_newline_splits_line) {
    LineBuffer b = make("abcdef");
    b.replace({0, 3}, {0, 3}, "\n");
    CHECK_EQ(b.lineCount(), std::size_t{2});
    CHECK_EQ(b.line(0), std::string("abc"));
    CHECK_EQ(b.line(1), std::string("def"));
}

TEST(buffer_delete_across_lines_joins) {
    LineBuffer b = make("abc\ndef\nghi");
    b.replace({0, 2}, {2, 1}, "");
    CHECK_EQ(b.lineCount(), std::size_t{1});
    CHECK_EQ(b.line(0), std::string("abhi"));
}

TEST(buffer_text_range) {
    LineBuffer b = make("abc\ndef\nghi");
    CHECK_EQ(b.text({0, 1}, {2, 2}), std::string("bc\ndef\ngh"));
    CHECK_EQ(b.text({1, 0}, {1, 3}), std::string("def"));
}

TEST(buffer_replace_text_with_text) {
    LineBuffer b = make("foo bar");
    b.replace({0, 4}, {0, 7}, "baz\nqux");
    CHECK_EQ(all(b), std::string("foo baz\nqux"));
}

TEST(history_undo_redo_single_edit) {
    LineBuffer b;
    UndoHistory h;
    h.record(b.replace({0, 0}, {0, 0}, "hello"));
    h.close();
    CHECK_EQ(b.line(0), std::string("hello"));

    CHECK(h.undo(b).has_value());
    CHECK_EQ(b.line(0), std::string(""));
    CHECK(h.redo(b).has_value());
    CHECK_EQ(b.line(0), std::string("hello"));
}

TEST(history_groups_edits_until_close) {
    LineBuffer b;
    UndoHistory h;
    h.record(b.replace({0, 0}, {0, 0}, "a"));
    h.record(b.replace({0, 1}, {0, 1}, "b"));
    h.record(b.replace({0, 2}, {0, 2}, "c"));
    h.close();
    h.record(b.replace({0, 3}, {0, 3}, "d"));
    h.close();

    h.undo(b);
    CHECK_EQ(b.line(0), std::string("abc"));
    h.undo(b);
    CHECK_EQ(b.line(0), std::string(""));
}

TEST(history_undo_restores_multiline_delete) {
    LineBuffer b = make("one\ntwo\nthree");
    UndoHistory h;
    h.record(b.replace({0, 1}, {2, 2}, ""));
    h.close();
    CHECK_EQ(all(b), std::string("oree"));
    h.undo(b);
    CHECK_EQ(all(b), std::string("one\ntwo\nthree"));
}

TEST(history_nothing_to_undo) {
    LineBuffer b;
    UndoHistory h;
    CHECK(!h.undo(b).has_value());
    CHECK(!h.redo(b).has_value());
}

TEST(history_new_edit_clears_redo) {
    LineBuffer b;
    UndoHistory h;
    h.record(b.replace({0, 0}, {0, 0}, "a"));
    h.close();
    h.undo(b);
    h.record(b.replace({0, 0}, {0, 0}, "z"));
    h.close();
    CHECK(!h.redo(b).has_value());
    CHECK_EQ(b.line(0), std::string("z"));
}

TEST(history_modified_flag) {
    LineBuffer b;
    UndoHistory h;
    CHECK(!h.modified());
    h.record(b.replace({0, 0}, {0, 0}, "x"));
    h.close();
    CHECK(h.modified());
    h.markSaved();
    CHECK(!h.modified());
    h.undo(b);
    CHECK(h.modified());
    h.redo(b);
    CHECK(!h.modified());
}
