#pragma once

#include <optional>
#include <string>

namespace qalam {

    bool clipboardWrite(const std::string& text);
    std::optional<std::string> clipboardRead();

}