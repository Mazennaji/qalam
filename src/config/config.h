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
        std::vector<std::string> errors;
        int timeoutMs = 500;
        bool found = false;
    };

    std::string defaultConfigPath();

    Config loadConfig(const std::string& path);

}