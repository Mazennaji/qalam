#pragma once

#include <map>
#include <string>
#include <vector>

#include "input/keymap.h"
#include "terminal/terminal.h"

namespace qalam {

    struct Mapping {
        MapMode mode;
        std::vector<Key> lhs;
        std::vector<Key> rhs;
    };

    struct Config {
        std::vector<Mapping> mappings;
        std::map<std::string, std::string> aliases;
        std::vector<std::pair<std::string, int>> colors;
        std::vector<std::string> errors;
        int timeoutMs = 500;
        bool syntax = true;
        bool number = false;
        bool relativeNumber = false;
        bool wrap = false;
        bool bidi = false;
        int tabstop = 4;
        int shiftwidth = 0;
        bool expandtab = false;
        bool autoindent = false;
        bool history = true;
        bool found = false;
    };

    std::string defaultConfigPath();
    std::string defaultHistoryPath();

    Config loadConfig(const std::string& path);

}