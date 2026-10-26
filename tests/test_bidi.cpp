#include <algorithm>
#include <string>
#include <vector>

#include "check.h"
#include "text/bidi.h"

using namespace qalam;

namespace {
    const std::string kQlm = "\xd9\x82\xd9\x84\xd9\x85";

    bool covers(const std::string &line, const std::vector<VisualRun> &runs) {
        std::vector<int> seen(line.size(), 0);
        for (const VisualRun &r: runs) {
            if (r.begin >= r.end || r.end > line.size()) return false;
            for (std::size_t i = r.begin; i < r.end; ++i) ++seen[i];
        }
        return std::all_of(seen.begin(), seen.end(), [](int c) { return c == 1; });
    }

    void expectRun(const std::vector<VisualRun> &runs, std::size_t index, std::size_t begin,
                   std::size_t end, bool rtl) {
        CHECK(index < runs.size());
        if (index >= runs.size()) return;
        CHECK_EQ(runs[index].begin, begin);
        CHECK_EQ(runs[index].end, end);
        CHECK_EQ(runs[index].rtl, rtl);
    }
}

TEST(bidi_empty_line_has_no_runs) {
    CHECK(visualRuns("").empty());
}

TEST(bidi_pure_latin_is_one_ltr_run) {
    const auto runs = visualRuns("hello world");
    CHECK_EQ(runs.size(), std::size_t{1});
    expectRun(runs, 0, 0, 11, false);
}

TEST(bidi_pure_arabic_is_one_rtl_run) {
    const auto runs = visualRuns(kQlm);
    CHECK_EQ(runs.size(), std::size_t{1});
    expectRun(runs, 0, 0, 6, true);
}

TEST(bidi_only_neutrals_use_the_default_direction) {
    const auto runs = visualRuns("   ");
    CHECK_EQ(runs.size(), std::size_t{1});
    expectRun(runs, 0, 0, 3, false);
    CHECK_EQ(visualRuns("1 2 3").size(), std::size_t{1});
}

TEST(bidi_arabic_inside_a_latin_line) {
    const std::string line = "ab " + kQlm;
    const auto runs = visualRuns(line);
    CHECK_EQ(runs.size(), std::size_t{2});
    expectRun(runs, 0, 0, 3, false);
    expectRun(runs, 1, 3, 9, true);
}

TEST(bidi_latin_inside_an_arabic_line) {
    const std::string line = kQlm + " abc";
    const auto runs = visualRuns(line);
    CHECK_EQ(runs.size(), std::size_t{2});
    expectRun(runs, 0, 7, 10, false);
    expectRun(runs, 1, 0, 7, true);
}

TEST(bidi_digits_stay_in_reading_order_inside_arabic) {
    const std::string line = kQlm + " 123";
    const auto runs = visualRuns(line);
    CHECK_EQ(runs.size(), std::size_t{2});
    expectRun(runs, 0, 7, 10, false);
    expectRun(runs, 1, 0, 7, true);
}

TEST(bidi_digits_after_arabic_in_a_latin_line) {
    const std::string line = "ab " + kQlm + " 12";
    const auto runs = visualRuns(line);
    CHECK_EQ(runs.size(), std::size_t{3});
    expectRun(runs, 0, 0, 3, false);
    expectRun(runs, 1, 10, 12, false);
    expectRun(runs, 2, 3, 10, true);
}

TEST(bidi_digits_after_latin_do_not_split_the_run) {
    const auto runs = visualRuns("a 1");
    CHECK_EQ(runs.size(), std::size_t{1});
}

TEST(bidi_neutral_between_different_directions_follows_the_paragraph) {
    const std::string ltr = "a " + kQlm;
    expectRun(visualRuns(ltr), 0, 0, 2, false);

    const std::string rtl = kQlm + " a";
    const auto runs = visualRuns(rtl);
    expectRun(runs, 0, 7, 8, false);
    expectRun(runs, 1, 0, 7, true);
}

TEST(bidi_combining_marks_stay_with_their_base) {
    const std::string vocalized = "\xd9\x82\xd9\x8e\xd9\x84\xd9\x8e\xd9\x85";
    const auto runs = visualRuns(vocalized);
    CHECK_EQ(runs.size(), std::size_t{1});
    expectRun(runs, 0, 0, vocalized.size(), true);

    const std::string accent = "e\xcc\x81x";
    CHECK_EQ(visualRuns(accent).size(), std::size_t{1});
}

TEST(bidi_hebrew_is_right_to_left_too) {
    const std::string hebrew = "\xd7\x90\xd7\x91";
    const auto runs = visualRuns(hebrew);
    CHECK_EQ(runs.size(), std::size_t{1});
    CHECK(runs[0].rtl);
}

TEST(bidi_arabic_indic_digits_read_left_to_right) {
    const std::string line = kQlm + " \xd9\xa1\xd9\xa2";
    const auto runs = visualRuns(line);
    CHECK_EQ(runs.size(), std::size_t{2});
    CHECK(!runs[0].rtl);
}

TEST(bidi_runs_always_cover_the_whole_line) {
    const std::vector<std::string> pieces = {
        "a", "bc", " ", ",", "1", "23",
        kQlm, "\xd9\x8e", "\xd7\x90", "\xe6\x97\xa5",
        "\xcc\x81", "\xff", "\xd9", "\n"
    };
    unsigned seed = 4242;
    for (int round = 0; round < 3000; ++round) {
        std::string line;
        seed = seed * 1103515245u + 12345u;
        const std::size_t count = (seed >> 16) % 12;
        for (std::size_t i = 0; i < count; ++i) {
            seed = seed * 1103515245u + 12345u;
            line += pieces[(seed >> 16) % pieces.size()];
        }
        CHECK(covers(line, visualRuns(line)));
    }
}

TEST(bidi_random_bytes_never_break_coverage) {
    unsigned seed = 77;
    for (int round = 0; round < 2000; ++round) {
        std::string line;
        for (int i = 0; i < 16; ++i) {
            seed = seed * 1103515245u + 12345u;
            line += static_cast<char>((seed >> 16) & 0xff);
        }
        CHECK(covers(line, visualRuns(line)));
    }
}
