#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "buffer/text_buffer.h"

namespace qalam {
    class LineBuffer : public TextBuffer {
    public:
        LineBuffer();

        std::size_t lineCount() const override;

        const std::string &line(std::size_t index) const override;

        void assign(std::vector<std::string> lines) override;

        std::string text(Pos start, Pos end) const override;

        Edit replace(Pos start, Pos end, const std::string &inserted) override;

    private:
        struct Slot {
            std::size_t chunk;
            std::size_t offset;
        };

        Slot locate(std::size_t row) const;

        std::string &lineRef(std::size_t row);

        void rebuild(std::size_t from);

        void split(std::size_t chunk);

        void insertRows(std::size_t row, std::vector<std::string> rows);

        void eraseRows(std::size_t row, std::size_t count);

        std::vector<std::vector<std::string> > chunks_;
        std::vector<std::size_t> starts_;
        std::size_t count_ = 0;
    };
}
