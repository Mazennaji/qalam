#pragma once

#include <string>

#include "buffer/pos.h"

namespace qalam {

    struct Edit {
        Pos start;
        std::string removed;
        std::string inserted;
    };

}