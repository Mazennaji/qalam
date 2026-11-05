#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "finder/fuzzy.h"
#include "harness.h"

using namespace qalam;
namespace fs = std::filesystem;

TEST(fuzzy_requires_a_subsequence) {
    CHECK(fuzzyScore("abc", "a_b_c").has_value());
    CHECK(!fuzzyScore("abc", "acb").has_value());
    CHECK(!fuzzyScore("abcd", "abc").has_value());
    CHECK(fuzzyScore("", "anything").has_value());
}

TEST(fuzzy_is_case_insensitive_until_the_query_has_a_capital) {
    CHECK(fuzzyScore("edit", "src/Editor.cpp").has_value());
    CHECK(fuzzyScore("Edit", "src/Editor.cpp").has_value());
    CHECK(!fuzzyScore("Edit", "src/editor.cpp").has_value());
}

TEST(fuzzy_prefers_consecutive_matches) {
    const auto tight = fuzzyScore("edit", "editor.cpp");
    const auto loose = fuzzyScore("edit", "e_d_i_t.cpp");
    CHECK(tight.has_value() && loose.has_value());
    CHECK(*tight > *loose);
}

TEST(fuzzy_prefers_word_boundaries) {
    const auto boundary = fuzzyScore("lb", "line_buffer.cpp");
    const auto buried = fuzzyScore("lb", "globalbuffer.cpp");
    CHECK(boundary.has_value() && buried.has_value());
    CHECK(*boundary > *buried);
}

TEST(fuzzy_ranks_best_first_and_respects_the_limit) {
    const std::vector<std::string> files = {
        "src/buffer/line_buffer.cpp", "src/editor/editor.cpp",
        "tests/test_editor.cpp", "README.md",
        "src/editor/editor.h"
    };
    const auto hits = rankFuzzy("editor", files, 2);
    CHECK_EQ(hits.size(), std::size_t{2});
    CHECK(hits[0].find("editor") != std::string::npos);
    CHECK_EQ(rankFuzzy("zzz", files, 5).size(), std::size_t{0});
    CHECK_EQ(rankFuzzy("e", files, 100).size(), std::size_t{5});
}

TEST(fuzzy_exact_name_beats_longer_paths) {
    const std::vector<std::string> files = {"a/b/c/d/readme_notes.txt", "README.md"};
    CHECK_EQ(rankFuzzy("readme", files, 1).front(), std::string("README.md"));
}

TEST(fuzzy_long_inputs_stay_fast_and_correct) {
    const std::string path(5000, 'x');
    CHECK(fuzzyScore("xxxx", path).has_value());
    CHECK(!fuzzyScore("xxxy", path).has_value());
}

TEST(list_files_skips_hidden_and_build_directories) {
    const fs::path root = fs::temp_directory_path() / "qalam_find_list";
    fs::remove_all(root);
    fs::create_directories(root / "src");
    fs::create_directories(root / ".git");
    fs::create_directories(root / "node_modules" / "pkg");
    fs::create_directories(root / "build-x");
    std::ofstream(root / "src" / "main.cpp") << "x";
    std::ofstream(root / "top.txt") << "x";
    std::ofstream(root / ".hidden") << "x";
    std::ofstream(root / ".git" / "config") << "x";
    std::ofstream(root / "node_modules" / "pkg" / "index.js") << "x";
    std::ofstream(root / "build-x" / "out.o") << "x";

    std::vector<std::string> files = listFiles(root.string(), 100);
    std::sort(files.begin(), files.end());
    CHECK_EQ(files.size(), std::size_t{2});
    CHECK_EQ(files[0], std::string("src/main.cpp"));
    CHECK_EQ(files[1], std::string("top.txt"));

    CHECK_EQ(listFiles(root.string(), 1).size(), std::size_t{1});
    CHECK(listFiles((root / "missing").string(), 10).empty());
    fs::remove_all(root);
}

TEST(find_command_opens_the_best_match) {
    const fs::path root = fs::temp_directory_path() / "qalam_find_cmd";
    fs::remove_all(root);
    fs::create_directories(root / "src");
    std::ofstream(root / "src" / "alpha.txt") << "alpha body\n";
    std::ofstream(root / "src" / "beta.txt") << "beta body\n";

    const fs::path before = fs::current_path();
    fs::current_path(root);

    Ed ed("");
    ed.feed(":find alp<CR>");
    CHECK_EQ(ed.text(), std::string("alpha body"));

    ed.feed(":find bet<CR>");
    CHECK_EQ(ed.text(), std::string("beta body"));

    ed.feed(":fb alp<CR>");
    CHECK_EQ(ed.text(), std::string("alpha body"));

    ed.feed(":find nothinghere<CR>");
    CHECK(ed.e.message().find("No file matches") != std::string::npos);

    ed.feed(":find<CR>");
    CHECK(ed.e.message().find("usage") != std::string::npos);

    fs::current_path(before);
    fs::remove_all(root);
}

TEST(fuzzy_positions_follow_the_query_in_order) {
    const std::vector<std::size_t> p = fuzzyPositions("edc", "src/editor/editor.cpp");
    CHECK_EQ(p.size(), std::size_t{3});
    CHECK(p[0] < p[1] && p[1] < p[2]);
    CHECK_EQ(std::string("src/editor/editor.cpp").substr(p[0], 1), std::string("e"));
}

TEST(fuzzy_positions_empty_when_there_is_no_match) {
    CHECK(fuzzyPositions("zzz", "abc").empty());
    CHECK(fuzzyPositions("", "abc").empty());
    CHECK(fuzzyPositions("abcd", "abc").empty());
}

TEST(fuzzy_positions_respect_smartcase) {
    CHECK_EQ(fuzzyPositions("ed", "Editor").size(), std::size_t{2});
    CHECK(fuzzyPositions("Ed", "editor").empty());
}

TEST(fuzzy_positions_agree_with_the_score) {
    const char* names[] = {"line_buffer.cpp", "README.md", "a/b/c.txt"};
    for (const char* n : names) {
        for (const char* q : {"l", "lb", "re", "abc", "zz"}) {
            const bool scored = fuzzyScore(q, n).has_value();
            const bool placed = !fuzzyPositions(q, n).empty();
            CHECK_EQ(scored, placed);
        }
    }
}
