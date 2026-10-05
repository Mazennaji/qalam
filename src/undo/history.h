#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "buffer/edit.h"
#include "buffer/pos.h"
#include "buffer/text_buffer.h"

namespace qalam {

    class UndoHistory {
    public:
        void record(Edit edit);
        void close();

        std::optional<Pos> undo(TextBuffer& buffer);
        std::optional<Pos> redo(TextBuffer& buffer);

        void markSaved();
        bool modified() const;
        void clear();

    private:
        using Group = std::vector<Edit>;

        std::vector<Group> undo_;
        std::vector<Group> redo_;
        Group open_;
        std::size_t savePoint_ = 0;
    };

}