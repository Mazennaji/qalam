#include "config/config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>

#include "input/key_notation.h"

namespace qalam {
    namespace {
        std::vector<std::string> splitWords(const std::string &line) {
            std::vector<std::string> words;
            std::istringstream stream(line);
            std::string word;
            while (stream >> word) words.push_back(word);
            return words;
        }

        std::optional<MapMode> parseMode(const std::string &name) {
            if (name == "normal" || name == "n") return MapMode::Normal;
            if (name == "insert" || name == "i") return MapMode::Insert;
            if (name == "visual" || name == "v") return MapMode::Visual;
            return std::nullopt;
        }

        bool parseMapLine(const std::vector<std::string> &words, Config &config, std::string &error) {
            if (words.size() != 4) {
                error = "usage: map MODE KEYS KEYS (write spaces as <Space>)";
                return false;
            }

            const std::optional<MapMode> mode = parseMode(words[1]);
            if (!mode) {
                error = "unknown mode '" + words[1] + "' (use normal, insert, or visual)";
                return false;
            }

            const std::optional<std::vector<Key> > lhs = parseKeys(words[2], error);
            if (!lhs) return false;
            if (lhs->empty()) {
                error = "empty left-hand side";
                return false;
            }

            const std::optional<std::vector<Key> > rhs = parseKeys(words[3], error);
            if (!rhs) return false;

            config.mappings.push_back({*mode, *lhs, *rhs});
            return true;
        }

        bool parseAliasLine(const std::vector<std::string> &words, Config &config, std::string &error) {
            if (words.size() != 3) {
                error = "usage: alias NAME COMMAND";
                return false;
            }
            config.aliases[words[1]] = words[2];
            return true;
        }

        bool parseColorLine(const std::vector<std::string> &words, Config &config, std::string &error) {
            if (words.size() != 3) {
                error = "usage: color NAME CODE (CODE is 0-255)";
                return false;
            }

            static const char *const names[] = {
                "keyword", "type", "string",
                "comment", "number", "preproc"
            };
            const bool known = std::any_of(std::begin(names), std::end(names),
                                           [&](const char *name) { return words[1] == name; });
            if (!known) {
                error = "unknown color name '" + words[1] + "'";
                return false;
            }

            const std::string &value = words[2];
            const bool digits = !value.empty() && value.size() <= 3 &&
                                std::all_of(value.begin(), value.end(), [](char c) {
                                    return std::isdigit(static_cast<unsigned char>(c)) != 0;
                                });
            const int code = digits ? std::stoi(value) : -1;
            if (code < 0 || code > 255) {
                error = "color code must be between 0 and 255";
                return false;
            }

            config.colors.emplace_back(words[1], code);
            return true;
        }

        bool parseSetLine(const std::string &line, Config &config, std::string &error) {
            std::string flat = line;
            std::replace(flat.begin(), flat.end(), '=', ' ');
            const std::vector<std::string> words = splitWords(flat);

            if (words.size() == 2) {
                std::string name = words[1];
                bool value = true;
                if (name.size() > 2 && name.compare(0, 2, "no") == 0) {
                    value = false;
                    name = name.substr(2);
                }

                if (name == "number" || name == "nu") {
                    config.number = value;
                } else if (name == "relativenumber" || name == "rnu") {
                    config.relativeNumber = value;
                } else if (name == "wrap") {
                    config.wrap = value;
                } else if (name == "syntax") {
                    config.syntax = value;
                } else {
                    error = "unknown option '" + words[1] + "'";
                    return false;
                }
                return true;
            }

            if (words.size() != 3) {
                error = "usage: set OPTION VALUE";
                return false;
            }

            if (words[1] == "syntax") {
                if (words[2] == "on") {
                    config.syntax = true;
                } else if (words[2] == "off") {
                    config.syntax = false;
                } else {
                    error = "syntax must be on or off";
                    return false;
                }
                return true;
            }

            if (words[1] != "timeoutlen") {
                error = "unknown option '" + words[1] + "'";
                return false;
            }

            const std::string &value = words[2];
            const bool digits = !value.empty() && value.size() <= 5 &&
                                std::all_of(value.begin(), value.end(), [](char c) {
                                    return std::isdigit(static_cast<unsigned char>(c)) != 0;
                                });
            const int ms = digits ? std::stoi(value) : 0;
            if (ms < 1 || ms > 10000) {
                error = "timeoutlen must be between 1 and 10000";
                return false;
            }
            config.timeoutMs = ms;
            return true;
        }

        bool parseLine(const std::string &line, Config &config, std::string &error) {
            const std::size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos) return true;
            if (line[start] == '#' || line[start] == '"') return true;

            const std::vector<std::string> words = splitWords(line);
            const std::string &command = words[0];

            if (command == "map") return parseMapLine(words, config, error);
            if (command == "alias") return parseAliasLine(words, config, error);
            if (command == "color") return parseColorLine(words, config, error);
            if (command == "set") return parseSetLine(line, config, error);

            error = "unknown command '" + command + "'";
            return false;
        }
    }

    std::string defaultConfigPath() {
        if (const char *env = std::getenv("QALAM_CONFIG"); env != nullptr && *env != '\0') {
            return env;
        }
        if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
            return std::string(xdg) + "/qalam/keymap.conf";
        }
        if (const char *home = std::getenv("HOME"); home != nullptr && *home != '\0') {
            return std::string(home) + "/.config/qalam/keymap.conf";
        }
        return "";
    }

    Config loadConfig(const std::string &path) {
        Config config;
        if (path.empty()) return config;

        std::ifstream in(path);
        if (!in) return config;
        config.found = true;

        std::string line;
        std::size_t number = 0;
        while (std::getline(in, line)) {
            ++number;
            if (!line.empty() && line.back() == '\r') line.pop_back();

            std::string error;
            if (!parseLine(line, config, error)) {
                config.errors.push_back("line " + std::to_string(number) + ": " + error);
            }
        }
        return config;
    }
}
