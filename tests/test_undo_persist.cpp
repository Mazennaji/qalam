#include <filesystem>
#include <fstream>
#include <string>

#include "harness.h"
#include "undo/history.h"

using namespace qalam;
namespace fs = std::filesystem;

namespace {
    struct Dir {
        fs::path root = fs::temp_directory_path() / "qalam_undo";
        Dir() {
            fs::remove_all(root);
            fs::create_directories(root);
        }
        ~Dir() { fs::remove_all(root); }
        std::string file() const { return (root / "doc.txt").string(); }
        std::string hist() const { return (root / "state" / "history").string(); }
    };
}

TEST(history_serialize_round_trip) {
    LineBuffer b;
    UndoHistory h;
    h.record(b.replace({0, 0}, {0, 0}, "hello\nworld"));
    h.close();
    h.record(b.replace({1, 0}, {1, 5}, ""));
    h.close();
    h.undo(b);

    UndoHistory copy;
    CHECK(copy.deserialize(h.serialize()));
    CHECK(!copy.modified());
    CHECK(copy.undo(b).has_value());
    CHECK(copy.redo(b).has_value());
}

TEST(history_deserialize_rejects_garbage) {
    UndoHistory h;
    CHECK(!h.deserialize(""));
    CHECK(!h.deserialize("nonsense"));
    CHECK(!h.deserialize("U 1\nG 1\nE 0 0 99 0\nab\nR 0\n"));
    CHECK(!h.deserialize("U -1\nR 0\n"));
    CHECK(!h.deserialize("U 0\nR 0\nextra"));
    CHECK(h.deserialize("U 0\nR 0\n"));
}

TEST(undo_survives_a_restart) {
    Dir d;
    std::ofstream(d.file()) << "abc\n";
    {
        Ed ed("");
        ed.e.open(d.file());
        ed.e.loadHistory(d.hist());
        ed.feed("xx:w<CR>");
        CHECK_EQ(ed.text(), std::string("c"));
    }

    Ed again("");
    again.e.open(d.file());
    again.e.loadHistory(d.hist());
    CHECK_EQ(again.text(), std::string("c"));
    again.feed("u");
    CHECK_EQ(again.text(), std::string("bc"));
    again.feed("u");
    CHECK_EQ(again.text(), std::string("abc"));
    again.feed("<C-r>");
    CHECK_EQ(again.text(), std::string("bc"));
}

TEST(redo_stack_survives_too) {
    Dir d;
    std::ofstream(d.file()) << "abc\n";
    {
        Ed ed("");
        ed.e.open(d.file());
        ed.e.loadHistory(d.hist());
        ed.feed("xxu:w<CR>");
        CHECK_EQ(ed.text(), std::string("bc"));
    }
    Ed again("");
    again.e.open(d.file());
    again.e.loadHistory(d.hist());
    again.feed("<C-r>");
    CHECK_EQ(again.text(), std::string("c"));
}

TEST(restored_history_is_not_marked_modified) {
    Dir d;
    std::ofstream(d.file()) << "abc\n";
    {
        Ed ed("");
        ed.e.open(d.file());
        ed.e.loadHistory(d.hist());
        ed.feed("x:w<CR>");
    }
    Ed again("");
    again.e.open(d.file());
    again.e.loadHistory(d.hist());
    again.feed(":q<CR>");
    CHECK(again.e.shouldQuit());
}

TEST(history_is_dropped_when_the_file_changed_outside) {
    Dir d;
    std::ofstream(d.file()) << "abc\n";
    {
        Ed ed("");
        ed.e.open(d.file());
        ed.e.loadHistory(d.hist());
        ed.feed("x:w<CR>");
    }
    std::ofstream(d.file(), std::ios::trunc) << "different\n";

    Ed again("");
    again.e.open(d.file());
    again.e.loadHistory(d.hist());
    again.feed("u");
    CHECK_EQ(again.text(), std::string("different"));
}

TEST(corrupt_undo_files_are_ignored) {
    Dir d;
    std::ofstream(d.file()) << "abc\n";
    {
        Ed ed("");
        ed.e.open(d.file());
        ed.e.loadHistory(d.hist());
        ed.feed("x:w<CR>");
    }
    std::error_code ec;
    for (fs::directory_iterator it(d.root / "state" / "undo", ec), end; !ec && it != end;
         it.increment(ec)) {
        std::ofstream(it->path(), std::ios::trunc) << "QUNDO1\nH 1\nB 5\nxx";
         }
    Ed again("");
    again.e.open(d.file());
    again.e.loadHistory(d.hist());
    again.feed("u");
    CHECK_EQ(again.text(), std::string("bc"));
}

TEST(nohistory_writes_no_undo_files) {
    Dir d;
    std::ofstream(d.file()) << "abc\n";
    Ed ed("");
    ed.e.open(d.file());
    ed.feed("x:w<CR>");
    CHECK(!fs::exists(d.root / "state"));
}