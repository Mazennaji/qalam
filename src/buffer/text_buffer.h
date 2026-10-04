#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace qalam {

    class TextBuffer {
    public:
        virtual ~TextBuffer() = default;

        virtual std::size_t lineCount() const = 0;
        virtual const std::string& line(std::size_t index) const = 0;
        virtual void assign(std::vector<std::string> lines) = 0;
    };

}