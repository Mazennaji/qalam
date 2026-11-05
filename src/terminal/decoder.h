#pragma once

#include <cstddef>
#include <string>

#include "terminal/terminal.h"

namespace qalam {
    struct Decoded {
        Key key;
        std::size_t used = 0;
        bool needMore = false;
    };

    Decoded decodeKey(const std::string &bytes, bool final);
}
