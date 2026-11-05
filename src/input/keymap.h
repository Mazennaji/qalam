#pragma once

#include <vector>

#include "terminal/terminal.h"

namespace qalam {

    enum class MapMode { Normal, Insert, Visual };

    inline bool sameKey(const Key& a, const Key& b) {
        return a.type == b.type && a.ch == b.ch && a.text == b.text;
    }

    struct MapLookup {
        const std::vector<Key>* exact = nullptr;
        bool longer = false;
    };

    class Keymap {
    public:
        void add(MapMode mode, std::vector<Key> lhs, std::vector<Key> rhs);
        MapLookup find(MapMode mode, const std::vector<Key>& typed) const;
        bool remove(MapMode mode, const std::vector<Key>& lhs);

    private:
        struct Entry {
            std::vector<Key> lhs;
            std::vector<Key> rhs;
        };

        static std::size_t index(MapMode mode);

        std::vector<Entry> entries_[3];
    };

}