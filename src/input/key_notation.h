#pragma once

#include <optional>
#include <string>
#include <vector>

#include "terminal/terminal.h"

namespace qalam {

    std::optional<std::vector<Key>> parseKeys(const std::string& text, std::string& error);

}