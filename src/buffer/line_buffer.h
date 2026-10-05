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

        std::string text(Pos start, Pos end) const override;
        Edit replace(Pos start, Pos end, const std::string& inserted) override;

    private:
        std::vector<std::string> lines_;
    };

}