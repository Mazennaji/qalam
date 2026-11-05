#include <cstdio>
#include <filesystem>
#include <string>

#include "harness.h"
#include "render/render.h"

using qalam::Size;

namespace {
    Size screen(int rows, int cols) {
        Size size;
        size.rows = rows;
        size.cols = cols;
        return size;
    }

    bool has(const std::string &haystack, const std::string &needle) {
        return haystack.find(needle) != std::string::npos;
    }
}

TEST(render_shows_text_status_and_tildes) {
    Ed ed("hello\nworld");
    ed.e.resize(screen(10, 40));
    const std::string frame = qalam::renderFrame(screen(10, 40), ed.e.view());
    CHECK(has(frame, "hello"));
    CHECK(has(frame, "world"));
    CHECK(has(frame, "NORMAL"));
    CHECK(has(frame, "~"));
    CHECK(has(frame, "Ln 1, Col 1"));
}

TEST(render_mode_name_follows_mode) {
    Ed ed("abc");
    ed.e.resize(screen(10, 40));
    ed.feed("i");
    CHECK(has(qalam::renderFrame(screen(10, 40), ed.e.view()), "INSERT"));
    ed.feed("<Esc>v");
    CHECK(has(qalam::renderFrame(screen(10, 40), ed.e.view()), "VISUAL"));
    ed.feed("<Esc>V");
    CHECK(has(qalam::renderFrame(screen(10, 40), ed.e.view()), "V-LINE"));
}

TEST(render_command_line_is_shown) {
    Ed ed("abc");
    ed.e.resize(screen(10, 40));
    ed.feed(":set nu");
    CHECK(has(qalam::renderFrame(screen(10, 40), ed.e.view()), ":set nu"));
}

TEST(render_line_numbers_when_enabled) {
    Ed ed("a\nb\nc");
    ed.e.resize(screen(10, 40));
    ed.feed(":set nu<CR>");
    const std::string frame = qalam::renderFrame(screen(10, 40), ed.e.view());
    CHECK(has(frame, "  1 "));
    CHECK(has(frame, "  3 "));
}

TEST(render_modified_flag) {
    Ed ed("abc");
    ed.e.resize(screen(10, 40));
    CHECK(!has(qalam::renderFrame(screen(10, 40), ed.e.view()), "[+]"));
    ed.feed("x");
    CHECK(has(qalam::renderFrame(screen(10, 40), ed.e.view()), "[+]"));
}

TEST(render_wide_characters_do_not_overflow) {
    Ed ed("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
    ed.e.resize(screen(6, 7));
    const std::string frame = qalam::renderFrame(screen(6, 7), ed.e.view());
    CHECK(!frame.empty());
}

TEST(render_tiny_screens_do_not_crash) {
    Ed ed("some text\nmore");
    for (int rows = 1; rows <= 4; ++rows) {
        for (int cols = 1; cols <= 6; ++cols) {
            ed.e.resize(screen(rows, cols));
            const std::string frame = qalam::renderFrame(screen(rows, cols), ed.e.view());
            CHECK(!frame.empty());
        }
    }
}

TEST(render_split_windows_draw_both) {
    Ed ed("same");
    ed.e.resize(screen(12, 40));
    ed.feed(":vs<CR>");
    const std::string frame = qalam::renderFrame(screen(12, 40), ed.e.view());
    CHECK(has(frame, "same"));
}

TEST(render_search_highlight_is_present) {
    Ed ed("foo bar foo");
    ed.e.resize(screen(10, 40));
    ed.feed("/foo<CR>");
    const std::string frame = qalam::renderFrame(screen(10, 40), ed.e.view());
    CHECK(has(frame, "\x1b["));
    CHECK(has(frame, "foo"));
}

TEST(history_file_round_trip) {
    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "qalam_test_history";
    std::filesystem::remove(path);

    {
        Ed ed("a\nb\nc");
        ed.e.loadHistory(path.string());
        ed.feed(":1d<CR>");
        ed.feed("/b<CR>");
        ed.e.saveHistory();
    }
    CHECK(std::filesystem::exists(path));

    Ed again("a\nb\nc");
    again.e.loadHistory(path.string());
    again.feed(":<Up><CR>");
    CHECK_EQ(again.text(), std::string("b\nc"));

    again.feed("/<Up>");
    CHECK(again.e.mode() == qalam::Mode::Search);
    again.feed("<Esc>");

    std::filesystem::remove(path);
}

TEST(history_file_skips_duplicates_and_blank_lines) {
    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "qalam_test_history2";
    std::filesystem::remove(path);

    {
        Ed ed("a");
        ed.e.loadHistory(path.string());
        ed.feed(":set nu<CR>:set nu<CR>:<CR>");
        ed.e.saveHistory();
    }

    std::FILE *file = std::fopen(path.string().c_str(), "r");
    CHECK(file != nullptr);
    int lines = 0;
    if (file != nullptr) {
        int c = 0;
        while ((c = std::fgetc(file)) != EOF) {
            if (c == '\n') ++lines;
        }
        std::fclose(file);
    }
    CHECK_EQ(lines, 1);
    std::filesystem::remove(path);
}
