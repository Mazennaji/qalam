#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "buffer/text_buffer.h"

namespace qalam {

    enum class LoadStatus { Loaded, NotFound, Failed };

    struct LoadResult {
        LoadStatus status;
        std::size_t bytes;
    };

    LoadResult loadFile(const std::string& path, TextBuffer& buffer);

    std::optional<std::size_t> saveFile(const std::string& path, const TextBuffer& buffer);

}