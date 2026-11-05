#include <filesystem>
#include <fstream>
#include <string>

#include "check.h"
#include "config/config.h"
#include "input/key_notation.h"
#include "input/keymap.h"

using namespace qalam;

namespace {
    std::string writeTemp(const std::string &name, const std::string &body) {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
        std::ofstream out(path, std::ios::trunc);
        out << body;
        return path.string();
    }
}

TEST(keys_parse_named_keys) {
    std::string error;
    const auto esc = parseKeys("<Esc>", error);
    CHECK(esc.has_value());
    CHECK_EQ(esc->size(), std::size_t{1});
    CHECK(esc->front().type == KeyType::Escape);

    const auto ctrl = parseKeys("<C-r>", error);
    CHECK(ctrl.has_value());
    CHECK(ctrl->front().type == KeyType::Ctrl);
    CHECK_EQ(ctrl->front().ch, 'r');

    const auto mixed = parseKeys("dd<CR>", error);
    CHECK(mixed.has_value());
    CHECK_EQ(mixed->size(), std::size_t{3});
    CHECK(mixed->back().type == KeyType::Enter);
}

TEST(keys_parse_rejects_unknown_names) {
    std::string error;
    CHECK(!parseKeys("<Bogus>", error).has_value());
    CHECK(!error.empty());
}

TEST(keys_parse_literal_angle_brackets) {
    std::string error;
    const auto keys = parseKeys("<Lt>Esc<Gt>", error);
    CHECK(keys.has_value());
    CHECK_EQ(keys->size(), std::size_t{5});
    CHECK_EQ(keys->front().ch, '<');
    CHECK_EQ(keys->back().ch, '>');
}

TEST(keymap_exact_longer_and_remove) {
    std::string error;
    Keymap map;
    const auto lhs = *parseKeys("jk", error);
    const auto rhs = *parseKeys("<Esc>", error);
    map.add(MapMode::Insert, lhs, rhs);

    const auto first = *parseKeys("j", error);
    const MapLookup partial = map.find(MapMode::Insert, first);
    CHECK(partial.exact == nullptr);
    CHECK(partial.longer);

    const MapLookup full = map.find(MapMode::Insert, lhs);
    CHECK(full.exact != nullptr);

    CHECK(map.find(MapMode::Normal, lhs).exact == nullptr);

    CHECK(map.remove(MapMode::Insert, lhs));
    CHECK(map.find(MapMode::Insert, lhs).exact == nullptr);
    CHECK(!map.remove(MapMode::Insert, lhs));
}

TEST(keymap_later_mapping_replaces_earlier) {
    std::string error;
    Keymap map;
    const auto lhs = *parseKeys("zz", error);
    map.add(MapMode::Normal, lhs, *parseKeys("x", error));
    map.add(MapMode::Normal, lhs, *parseKeys("dd", error));
    const MapLookup found = map.find(MapMode::Normal, lhs);
    CHECK(found.exact != nullptr);
    CHECK_EQ(found.exact->size(), std::size_t{2});
}

TEST(config_missing_file) {
    const Config config = loadConfig("/nonexistent/qalam/none.conf");
    CHECK(!config.found);
    CHECK(config.errors.empty());
    CHECK_EQ(config.tabstop, 4);
    CHECK(config.syntax);
}

TEST(config_parses_maps_aliases_and_options) {
    const std::string path = writeTemp("qalam_test_a.conf",
                                       "# comment\n"
                                       "map normal <Space>w :w<CR>\n"
                                       "map insert jk <Esc>\n"
                                       "alias bye q\n"
                                       "set number\n"
                                       "set nowrap\n"
                                       "set tabstop=8\n"
                                       "set shiftwidth=2\n"
                                       "set expandtab\n"
                                       "set autoindent\n"
                                       "set nohistory\n"
                                       "set timeoutlen 300\n");
    const Config config = loadConfig(path);
    CHECK(config.found);
    CHECK(config.errors.empty());
    CHECK_EQ(config.mappings.size(), std::size_t{2});
    CHECK_EQ(config.aliases.at("bye"), std::string("q"));
    CHECK(config.number);
    CHECK(!config.wrap);
    CHECK_EQ(config.tabstop, 8);
    CHECK_EQ(config.shiftwidth, 2);
    CHECK(config.expandtab);
    CHECK(config.autoindent);
    CHECK(!config.history);
    CHECK_EQ(config.timeoutMs, 300);
}

TEST(config_collects_errors_and_keeps_going) {
    const std::string path = writeTemp("qalam_test_b.conf",
                                       "set bogus\n"
                                       "set tabstop=99\n"
                                       "map nowhere x y\n"
                                       "set number\n");
    const Config config = loadConfig(path);
    CHECK(config.found);
    CHECK_EQ(config.errors.size(), std::size_t{3});
    CHECK(config.number);
    CHECK_EQ(config.tabstop, 4);
}

TEST(config_colors) {
    const std::string path = writeTemp("qalam_test_c.conf", "color keyword 203\n");
    const Config config = loadConfig(path);
    CHECK(config.errors.empty());
    CHECK_EQ(config.colors.size(), std::size_t{1});
}
