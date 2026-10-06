#include "input/keymap.h"

#include <utility>

namespace qalam {

    std::size_t Keymap::index(MapMode mode) {
        return static_cast<std::size_t>(mode);
    }

    void Keymap::add(MapMode mode, std::vector<Key> lhs, std::vector<Key> rhs) {
        std::vector<Entry>& list = entries_[index(mode)];
        for (Entry& entry : list) {
            if (entry.lhs.size() != lhs.size()) continue;
            bool same = true;
            for (std::size_t i = 0; i < lhs.size(); ++i) {
                if (!sameKey(entry.lhs[i], lhs[i])) {
                    same = false;
                    break;
                }
            }
            if (same) {
                entry.rhs = std::move(rhs);
                return;
            }
        }
        list.push_back({std::move(lhs), std::move(rhs)});
    }

    MapLookup Keymap::find(MapMode mode, const std::vector<Key>& typed) const {
        MapLookup result;
        for (const Entry& entry : entries_[index(mode)]) {
            if (typed.size() > entry.lhs.size()) continue;

            bool prefix = true;
            for (std::size_t i = 0; i < typed.size(); ++i) {
                if (!sameKey(typed[i], entry.lhs[i])) {
                    prefix = false;
                    break;
                }
            }
            if (!prefix) continue;

            if (typed.size() == entry.lhs.size()) {
                result.exact = &entry.rhs;
            } else {
                result.longer = true;
            }
        }
        return result;
    }

}