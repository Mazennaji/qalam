#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace qalam {

    struct Rect {
        int row = 0;
        int col = 0;
        int rows = 0;
        int cols = 0;
    };

    struct Placement {
        int id = 0;
        Rect rect;
    };

    enum class Direction { Left, Down, Up, Right };

    struct LayoutNode {
        bool leaf = true;
        bool sideBySide = false;
        int id = 0;
        std::unique_ptr<LayoutNode> first;
        std::unique_ptr<LayoutNode> second;
    };

    class Layout {
    public:
        Layout();

        int split(int id, bool sideBySide);
        bool close(int id);
        void only(int id);
        std::size_t count() const;
        std::vector<int> order() const;
        std::vector<Placement> place(Rect area, std::vector<Rect>* separators = nullptr) const;
        std::optional<Rect> rectOf(Rect area, int id) const;
        std::optional<int> neighbor(Rect area, int id, Direction direction) const;

    private:
        std::unique_ptr<LayoutNode> root_;
        int nextId_ = 1;
    };

}