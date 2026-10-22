#include <string>
#include <vector>

#include "check.h"
#include "terminal/decoder.h"

using namespace qalam;

namespace {
    Decoded one(const std::string &bytes, bool final = false) {
        return decodeKey(bytes, final);
    }

    std::vector<Key> all(const std::string &bytes) {
        std::vector<Key> keys;
        std::string rest = bytes;
        while (!rest.empty()) {
            const Decoded d = decodeKey(rest, true);
            if (d.needMore || d.used == 0) break;
            keys.push_back(d.key);
            rest.erase(0, d.used);
        }
        return keys;
    }
}

TEST(decode_empty_needs_more) {
    CHECK(one("").needMore);
}

TEST(decode_printable) {
    const Decoded d = one("a");
    CHECK(d.key.type == KeyType::Char);
    CHECK_EQ(d.key.ch, 'a');
    CHECK_EQ(d.used, std::size_t{1});
    CHECK(one(" ").key.type == KeyType::Char);
    CHECK_EQ(one("~").key.ch, '~');
}

TEST(decode_control_keys) {
    const Decoded r = one("\x12");
    CHECK(r.key.type == KeyType::Ctrl);
    CHECK_EQ(r.key.ch, 'r');
    CHECK_EQ(one("\x16").key.ch, 'v');
    CHECK_EQ(one("\x17").key.ch, 'w');
    CHECK_EQ(one("\x03").key.ch, 'c');
}

TEST(decode_special_single_bytes) {
    CHECK(one("\r").key.type == KeyType::Enter);
    CHECK(one("\n").key.type == KeyType::Enter);
    CHECK(one("\t").key.type == KeyType::Tab);
    CHECK(one("\x7f").key.type == KeyType::Backspace);
    CHECK(one("\x08").key.type == KeyType::Backspace);
}

TEST(decode_nul_is_ignored_key) {
    const Decoded d = one(std::string(1, '\0'));
    CHECK(d.key.type == KeyType::None);
    CHECK_EQ(d.used, std::size_t{1});
}

TEST(decode_lone_escape_waits_then_resolves) {
    CHECK(one("\x1b").needMore);
    const Decoded d = one("\x1b", true);
    CHECK(!d.needMore);
    CHECK(d.key.type == KeyType::Escape);
    CHECK_EQ(d.used, std::size_t{1});
}

TEST(decode_arrow_keys_csi) {
    CHECK(one("\x1b[A").key.type == KeyType::Up);
    CHECK(one("\x1b[B").key.type == KeyType::Down);
    CHECK(one("\x1b[C").key.type == KeyType::Right);
    CHECK(one("\x1b[D").key.type == KeyType::Left);
    CHECK_EQ(one("\x1b[A").used, std::size_t{3});
}

TEST(decode_arrow_keys_ss3) {
    CHECK(one("\x1bOA").key.type == KeyType::Up);
    CHECK(one("\x1bOD").key.type == KeyType::Left);
    CHECK(one("\x1bOH").key.type == KeyType::Home);
    CHECK(one("\x1bOF").key.type == KeyType::End);
}

TEST(decode_home_end_variants) {
    CHECK(one("\x1b[H").key.type == KeyType::Home);
    CHECK(one("\x1b[F").key.type == KeyType::End);
    CHECK(one("\x1b[1~").key.type == KeyType::Home);
    CHECK(one("\x1b[7~").key.type == KeyType::Home);
    CHECK(one("\x1b[4~").key.type == KeyType::End);
    CHECK(one("\x1b[8~").key.type == KeyType::End);
}

TEST(decode_delete_and_paging) {
    CHECK(one("\x1b[3~").key.type == KeyType::Delete);
    CHECK(one("\x1b[5~").key.type == KeyType::PageUp);
    CHECK(one("\x1b[6~").key.type == KeyType::PageDown);
    CHECK_EQ(one("\x1b[3~").used, std::size_t{4});
}

TEST(decode_modified_arrows_use_the_base_key) {
    CHECK(one("\x1b[1;5C").key.type == KeyType::Right);
    CHECK(one("\x1b[1;2A").key.type == KeyType::Up);
    CHECK_EQ(one("\x1b[1;5C").used, std::size_t{6});
    CHECK(one("\x1b[3;5~").key.type == KeyType::Delete);
}

TEST(decode_partial_sequences_wait) {
    CHECK(one("\x1b[").needMore);
    CHECK(one("\x1b[1").needMore);
    CHECK(one("\x1b[1;").needMore);
    CHECK(one("\x1bO").needMore);
}

TEST(decode_partial_sequence_at_timeout_becomes_escape) {
    const Decoded d = one("\x1b[", true);
    CHECK(d.key.type == KeyType::Escape);
    CHECK_EQ(d.used, std::size_t{1});
}

TEST(decode_unknown_sequences_are_consumed_whole) {
    const Decoded d = one("\x1b[99~");
    CHECK(d.key.type == KeyType::None);
    CHECK_EQ(d.used, std::size_t{5});

    const Decoded z = one("\x1b[Z");
    CHECK(z.key.type == KeyType::None);
    CHECK_EQ(z.used, std::size_t{3});
}

TEST(decode_alt_key_is_escape_then_key) {
    const std::vector<Key> alt = all("\x1b" "x");
    CHECK_EQ(alt.size(), std::size_t{2});
    CHECK(alt[0].type == KeyType::Escape);
    CHECK(alt[1].type == KeyType::Char);
    CHECK_EQ(alt[1].ch, 'x');
}

TEST(decode_utf8_two_three_four_bytes) {
    const Decoded two = one("\xc3\xa9");
    CHECK(two.key.type == KeyType::Text);
    CHECK_EQ(two.key.text, std::string("\xc3\xa9"));
    CHECK_EQ(two.used, std::size_t{2});

    const Decoded three = one("\xe6\x97\xa5");
    CHECK_EQ(three.key.text, std::string("\xe6\x97\xa5"));
    CHECK_EQ(three.used, std::size_t{3});

    const Decoded four = one("\xf0\x9f\x98\x80");
    CHECK_EQ(four.key.text, std::string("\xf0\x9f\x98\x80"));
    CHECK_EQ(four.used, std::size_t{4});
}

TEST(decode_utf8_split_across_reads) {
    CHECK(one("\xe6").needMore);
    CHECK(one("\xe6\x97").needMore);
    CHECK(!one("\xe6\x97\xa5").needMore);
    CHECK(one("\xf0\x9f\x98").needMore);
}

TEST(decode_utf8_truncated_at_timeout_is_dropped) {
    const Decoded d = one("\xe6\x97", true);
    CHECK(d.key.type == KeyType::None);
    CHECK_EQ(d.used, std::size_t{2});
}

TEST(decode_invalid_utf8_makes_progress) {
    const Decoded stray = one("\x80");
    CHECK(stray.key.type == KeyType::None);
    CHECK_EQ(stray.used, std::size_t{1});

    const Decoded overlong = one("\xc0\x80");
    CHECK(overlong.key.type == KeyType::None);
    CHECK(overlong.used >= 1);

    const Decoded badTail = one("\xe6\x41\x41");
    CHECK(badTail.key.type == KeyType::None);
    CHECK_EQ(badTail.used, std::size_t{1});

    CHECK(one("\xff").key.type == KeyType::None);
}

TEST(decode_stream_of_mixed_keys) {
    const std::vector<Key> keys = all("ab\x1b[A\xc3\xa9\r\x12");
    CHECK_EQ(keys.size(), std::size_t{6});
    CHECK(keys[0].type == KeyType::Char);
    CHECK(keys[2].type == KeyType::Up);
    CHECK(keys[3].type == KeyType::Text);
    CHECK(keys[4].type == KeyType::Enter);
    CHECK(keys[5].type == KeyType::Ctrl);
}

TEST(decode_pasted_text_with_escape_inside) {
    const std::vector<Key> keys = all("a\x1b[Cb");
    CHECK_EQ(keys.size(), std::size_t{3});
    CHECK(keys[1].type == KeyType::Right);
}

TEST(decode_random_bytes_always_progress) {
    unsigned seed = 7;
    for (int round = 0; round < 500; ++round) {
        std::string bytes;
        for (int i = 0; i < 12; ++i) {
            seed = seed * 1103515245u + 12345u;
            bytes += static_cast<char>((seed >> 16) & 0xff);
        }
        std::string rest = bytes;
        int guard = 0;
        while (!rest.empty() && guard++ < 100) {
            const Decoded d = decodeKey(rest, true);
            CHECK(!d.needMore);
            CHECK(d.used >= 1);
            CHECK(d.used <= rest.size());
            rest.erase(0, d.used);
        }
        CHECK(rest.empty());
    }
}
