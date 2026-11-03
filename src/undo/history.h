#pragma once

#include <cstddef>
#include <optional>
#include <vector>
#include <cstdint>
#include <string>

#include "buffer/edit.h"
#include "buffer/pos.h"
#include "buffer/text_buffer.h"

namespace qalam {
    std::uint64_t fnv1a(const std::string& data);

    class UndoHistory {
    public:
        void record(Edit edit);
        void close();

        std::optional<Pos> undo(TextBuffer& buffer);
        std::optional<Pos> redo(TextBuffer& buffer);

        void markSaved();
        bool modified() const;
        std::size_t touchedRow() const;
        void clear();
        std::string serialize() const;
        bool deserialize(const std::string& data);

    private:
        using Group = std::vector<Edit>;

        std::vector<Group> undo_;
        std::vector<Group> redo_;
        Group open_;
        std::size_t savePoint_ = 0;
        std::size_t touched_ = 0;
    };

}