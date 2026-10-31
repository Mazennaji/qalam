#include <filesystem>
#include <fstream>
#include <string>

#include "harness.h"
#include "render/render.h"

using namespace qalam;
namespace fs = std::filesystem;

namespace {
    struct Project {
        fs::path root;
        fs::path before;

        Project() {
            root = fs::temp_directory_path() / "qalam_picker";
            fs::remove_all(root);
            fs::create_directories(root);
            std::ofstream(root / "alpha.txt") << "alpha body\n";
            std::ofstream(root / "alpha2.txt") << "alpha2 body\n";
            std::ofstream(root / "beta.txt") << "beta body\n";
            before = fs::current_path();
            fs::current_path(root);
        }

        ~Project() {
            fs::current_path(before);
            fs::remove_all(root);
        }
    };

    bool has(const std::string& hay, const std::string& needle) {
        return hay.find(needle) != std::string::npos;
    }

}

TEST(picker_opens_filters_and_opens_the_match) {
    Project p;
    Ed ed("");
    ed.feed(":pick<CR>");
    CHECK(ed.e.mode() == Mode::Picker);
    CHECK_EQ(ed.e.view().overlay.size(), std::size_t{3});

    ed.feed("bet");
    CHECK_EQ(ed.e.view().overlay.size(), std::size_t{1});
    CHECK_EQ(ed.e.view().overlay[0], std::string("beta.txt"));

    ed.feed("<CR>");
    CHECK(ed.e.mode() == Mode::Normal);
    CHECK_EQ(ed.text(), std::string("beta body"));
}

TEST(picker_down_selects_the_next_match) {
    Project p;
    Ed ed("");
    ed.feed(":pick<CR>alp");
    CHECK_EQ(ed.e.view().overlay[0], std::string("alpha.txt"));
    ed.feed("<Down>");
    CHECK_EQ(ed.e.view().windows.size(), std::size_t{1});
    CHECK_EQ(ed.e.view().overlaySelected, 1);
    ed.feed("<CR>");
    CHECK_EQ(ed.text(), std::string("alpha2 body"));
}

TEST(picker_ctrl_keys_move_and_clear) {
    Project p;
    Ed ed("");
    ed.feed(":pick<CR>alp<C-n><C-p>");
    CHECK_EQ(ed.e.view().overlaySelected, 0);
    ed.feed("<C-u>");
    CHECK_EQ(ed.e.view().overlay.size(), std::size_t{3});
    ed.feed("<C-c>");
    CHECK(ed.e.mode() == Mode::Normal);
}

TEST(picker_escape_and_empty_backspace_cancel) {
    Project p;
    Ed ed("keep");
    ed.feed(":pick<CR>al<Esc>");
    CHECK(ed.e.mode() == Mode::Normal);
    CHECK_EQ(ed.text(), std::string("keep"));

    ed.feed(":pick<CR>a<BS>");
    CHECK(ed.e.mode() == Mode::Picker);
    ed.feed("<BS>");
    CHECK(ed.e.mode() == Mode::Normal);
    CHECK_EQ(ed.text(), std::string("keep"));
}

TEST(picker_no_matches_closes_with_a_message) {
    Project p;
    Ed ed("keep");
    ed.feed(":pick<CR>zzzz");
    CHECK_EQ(ed.e.view().overlay[0], std::string("(no matches)"));
    ed.feed("<CR>");
    CHECK(ed.e.mode() == Mode::Normal);
    CHECK(has(ed.e.message(), "No match"));
    CHECK_EQ(ed.text(), std::string("keep"));
}

TEST(picker_switches_buffers) {
    Project p;
    Ed ed("");
    ed.feed(":e alpha.txt<CR>:e beta.txt<CR>");
    CHECK_EQ(ed.text(), std::string("beta body"));
    ed.feed(":pickb<CR>alp<CR>");
    CHECK_EQ(ed.text(), std::string("alpha body"));
}

TEST(picker_buffers_needs_named_buffers) {
    Ed ed("");
    ed.feed(":pickb<CR>");
    CHECK(ed.e.mode() == Mode::Normal);
    CHECK(has(ed.e.message(), "No named buffers"));
}

TEST(picker_paste_goes_into_the_query) {
    Project p;
    Ed ed("");
    ed.feed(":pick<CR>");
    Key paste;
    paste.type = KeyType::Paste;
    paste.text = "bet";
    ed.e.handleKey(paste);
    CHECK_EQ(ed.e.view().overlay[0], std::string("beta.txt"));
}

TEST(picker_renders_the_prompt_and_list) {
    Project p;
    Ed ed("");
    Size size;
    size.rows = 12;
    size.cols = 40;
    ed.e.resize(size);
    ed.feed(":pick<CR>alp");
    const std::string frame = renderFrame(size, ed.e.view());
    CHECK(has(frame, "file> alp"));
    CHECK(has(frame, "alpha.txt"));
    CHECK(has(frame, "\x1b[7m"));
    CHECK(has(frame, "PICK"));
}

TEST(picker_survives_tiny_screens) {
    Project p;
    Ed ed("");
    ed.feed(":pick<CR>");
    for (int rows = 1; rows <= 6; ++rows) {
        for (int cols = 1; cols <= 8; ++cols) {
            Size size;
            size.rows = rows;
            size.cols = cols;
            ed.e.resize(size);
            CHECK(!renderFrame(size, ed.e.view()).empty());
        }
    }
}

TEST(picker_emphasizes_matched_characters) {
    Project p;
    Ed ed("");
    Size size;
    size.rows = 12;
    size.cols = 40;
    ed.e.resize(size);
    ed.feed(":pick<CR>bet");
    CHECK(!ed.e.view().overlayMarks.empty());
    CHECK_EQ(ed.e.view().overlayMarks[0].size(), std::size_t{3});
    CHECK(has(renderFrame(size, ed.e.view()), "\x1b[1;4m"));
}