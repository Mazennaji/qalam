#include <filesystem>
#include <fstream>
#include <string>
#include <cstdio>

#include "harness.h"

using namespace qalam;
namespace fs = std::filesystem;

namespace {

    Key click(int x, int y, bool press) {
        Key key;
        key.type = KeyType::Mouse;
        key.button = 0;
        key.x = x;
        key.y = y;
        key.press = press;
        return key;
    }
}
TEST(mouse_option_defaults_on_and_toggles) {
    Ed ed("hello");
    CHECK(ed.e.mouseEnabled());
    ed.feed(":set nomouse<CR>");
    CHECK(!ed.e.mouseEnabled());
    ed.feed(":set<CR>");
    CHECK(ed.e.message().find("nomouse") != std::string::npos);
    ed.feed(":set mouse<CR>");
    CHECK(ed.e.mouseEnabled());
}

TEST(nomouse_ignores_clicks) {
    Ed ed("hello world");
    ed.feed(":set nomouse<CR>");
    ed.e.handleKey(click(6, 0, true));
    ed.e.handleKey(click(6, 0, false));
    CHECK_EQ(ed.col(), std::size_t{0});
    ed.feed(":set mouse<CR>");
    ed.e.handleKey(click(6, 0, true));
    ed.e.handleKey(click(6, 0, false));
    CHECK_EQ(ed.col(), std::size_t{6});
}

TEST(probe_mouse_messages) {
    Ed ed("hello");
    ed.feed(":set nomouse<CR>");
    std::printf("A enabled=%d msg=[%s]\n", ed.e.mouseEnabled() ? 1 : 0, ed.e.message().c_str());
    ed.feed(":set<CR>");
    std::printf("B msg=[%s]\n", ed.e.message().c_str());
    ed.feed(":set mouse<CR>");
    std::printf("C enabled=%d msg=[%s]\n", ed.e.mouseEnabled() ? 1 : 0, ed.e.message().c_str());
}

TEST(cursor_position_is_restored_on_reopen) {
    const fs::path dir = fs::temp_directory_path() / "qalam_session";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string file = (dir / "doc.txt").string();
    const std::string hist = (dir / "history").string();
    std::ofstream(file) << "one\ntwo\nthree two\nfour\n";

    {
        Ed ed("");
        ed.e.open(file);
        ed.e.loadHistory(hist);
        ed.feed("3Gw");
        CHECK_EQ(ed.row(), std::size_t{2});
        ed.e.saveHistory();
    }

    Ed again("");
    again.e.open(file);
    CHECK_EQ(again.row(), std::size_t{0});
    again.e.loadHistory(hist);
    CHECK_EQ(again.row(), std::size_t{2});
    CHECK_EQ(again.col(), std::size_t{6});
    fs::remove_all(dir);
}

TEST(restored_position_is_clamped_when_the_file_shrank) {
    const fs::path dir = fs::temp_directory_path() / "qalam_session2";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string file = (dir / "doc.txt").string();
    const std::string hist = (dir / "history").string();
    std::ofstream(file) << "a\nb\nc\nd\ne\n";

    {
        Ed ed("");
        ed.e.open(file);
        ed.e.loadHistory(hist);
        ed.feed("G");
        ed.e.saveHistory();
    }
    std::ofstream(file, std::ios::trunc) << "x\n";

    Ed again("");
    again.e.open(file);
    again.e.loadHistory(hist);
    CHECK_EQ(again.row(), std::size_t{0});
    fs::remove_all(dir);
}

TEST(position_lines_do_not_disturb_command_history) {
    const fs::path dir = fs::temp_directory_path() / "qalam_session3";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string file = (dir / "doc.txt").string();
    const std::string hist = (dir / "history").string();
    std::ofstream(file) << "a\nb\n";

    {
        Ed ed("");
        ed.e.open(file);
        ed.e.loadHistory(hist);
        ed.feed(":set nu<CR>");
        ed.e.saveHistory();
    }
    Ed again("a\nb");
    again.e.loadHistory(hist);
    again.feed(":<Up>");
    CHECK(again.e.mode() == Mode::Command);
    again.feed("<CR>");
    CHECK(again.e.message().find("Unknown option") == std::string::npos);
    fs::remove_all(dir);
}

TEST(garbled_position_lines_are_ignored) {
    const fs::path dir = fs::temp_directory_path() / "qalam_session4";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string hist = (dir / "history").string();
    std::ofstream(hist) << "@x:y:/nope\n@1:2\n@:\n:set nu\n";
    Ed ed("abc");
    ed.e.loadHistory(hist);
    CHECK_EQ(ed.text(), std::string("abc"));
    fs::remove_all(dir);
}

TEST(position_is_restored_by_edit_command) {
    const fs::path dir = fs::temp_directory_path() / "qalam_session5";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string file = (dir / "doc.txt").string();
    const std::string hist = (dir / "history").string();
    std::ofstream(file) << "a\nb\nc\nd\ne\n";

    {
        Ed ed("");
        ed.e.open(file);
        ed.e.loadHistory(hist);
        ed.feed("4G");
        ed.e.saveHistory();
    }

    Ed again("");
    again.e.loadHistory(hist);
    again.feed(":e " + file + "<CR>");
    CHECK_EQ(again.row(), std::size_t{3});
    fs::remove_all(dir);
}

TEST(position_keys_ignore_dot_slash_and_relative_paths) {
    const fs::path dir = fs::temp_directory_path() / "qalam_session6";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "doc.txt") << "a\nb\nc\nd\n";
    const std::string hist = (dir / "history").string();

    const fs::path before = fs::current_path();
    fs::current_path(dir);

    {
        Ed ed("");
        ed.e.open("doc.txt");
        ed.e.loadHistory(hist);
        ed.feed("3G");
        ed.e.saveHistory();
    }

    Ed again("");
    again.e.loadHistory(hist);
    again.feed(":e ./doc.txt<CR>");
    CHECK_EQ(again.row(), std::size_t{2});

    fs::current_path(before);
    fs::remove_all(dir);
}

TEST(edit_of_an_unknown_file_starts_at_the_top) {
    const fs::path dir = fs::temp_directory_path() / "qalam_session7";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string file = (dir / "fresh.txt").string();
    std::ofstream(file) << "a\nb\nc\n";

    Ed ed("");
    ed.e.loadHistory((dir / "history").string());
    ed.feed(":e " + file + "<CR>");
    CHECK_EQ(ed.row(), std::size_t{0});
    fs::remove_all(dir);
}