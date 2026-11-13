#include <sstream>
#include <string>

#include "harness.h"

using namespace qalam;

namespace {
    std::string lineOf(const std::string& text, std::size_t row) {
        std::istringstream in(text);
        std::string line;
        for (std::size_t i = 0; std::getline(in, line); ++i) {
            if (i == row) return line;
        }
        return "";
    }
}

TEST(help_opens_a_buffer_with_the_manual) {
    Ed ed("my text");
    ed.feed(":help<CR>");
    CHECK(ed.text().find("# qalam help") != std::string::npos);
    CHECK(ed.text().find(":pick") != std::string::npos);
    CHECK(ed.e.mode() == Mode::Normal);
}

TEST(help_does_not_replace_the_current_buffer) {
    Ed ed("my text");
    ed.feed("x:h<CR>:bp<CR>");
    CHECK_EQ(ed.text(), std::string("y text"));
}

TEST(help_topic_jumps_to_a_matching_line) {
    Ed ed("");
    ed.feed(":help macro<CR>");
    CHECK(ed.row() > 0);
    CHECK(lineOf(ed.text(), ed.row()).find("macro") != std::string::npos);
}

TEST(help_unknown_topic_reports_not_found) {
    Ed ed("");
    ed.feed(":help zzzqqq<CR>");
    CHECK(ed.e.message().find("not found") != std::string::npos);
}

TEST(help_buffer_does_not_block_quitting) {
    Ed ed("");
    ed.feed(":help<CR>:q<CR>");
    CHECK(ed.e.shouldQuit());
}

TEST(help_closes_with_bd) {
    Ed ed("keep");
    ed.feed(":help<CR>:bd<CR>");
    CHECK_EQ(ed.text(), std::string("keep"));
}