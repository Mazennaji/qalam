#pragma once

#include <string>
#include <vector>

#include "buffer/text_buffer.h"

namespace qalam {

    class LineBuffer : public TextBuffer {
    public:
        LineBuffer();

        std::size_t lineCount() const override;
        const std::string& line(std::size_t index) const override;
        void assign(std::vector<std::string> lines) override;

    private:
        std::vector<std::string> lines_;
    };

}