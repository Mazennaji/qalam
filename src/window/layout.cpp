#include "window/layout.h"

#include <cstdlib>
#include <utility>

namespace qalam {
namespace {

using Slot = std::unique_ptr<LayoutNode>;

Slot makeLeaf(int id) {
    auto node = std::make_unique<LayoutNode>();
    node->id = id;
    return node;
}

Slot* find(Slot& node, int id) {
    if (node->leaf) return node->id == id ? &node : nullptr;
    if (Slot* found = find(node->first, id)) return found;
    return find(node->second, id);
}

bool closeIn(Slot& node, int id) {
    if (node->leaf) return false;

    if (node->first->leaf && node->first->id == id) {
        Slot keep = std::move(node->second);
        node = std::move(keep);
        return true;
    }
    if (node->second->leaf && node->second->id == id) {
        Slot keep = std::move(node->first);
        node = std::move(keep);
        return true;
    }
    return closeIn(node->first, id) || closeIn(node->second, id);
}

void collect(const LayoutNode& node, std::vector<int>& out) {
    if (node.leaf) {
        out.push_back(node.id);
        return;
    }
    collect(*node.first, out);
    collect(*node.second, out);
}

void lay(const LayoutNode& node, Rect r, std::vector<Placement>& out,
         std::vector<Rect>* separators) {
    if (node.leaf) {
        out.push_back({node.id, r});
        return;
    }

    if (node.sideBySide) {
        const int left = (r.cols - 1) / 2;
        const int right = r.cols - 1 - left;
        lay(*node.first, {r.row, r.col, r.rows, left}, out, separators);
        if (separators != nullptr) separators->push_back({r.row, r.col + left, r.rows, 1});
        lay(*node.second, {r.row, r.col + left + 1, r.rows, right}, out, separators);
    } else {
        const int top = r.rows / 2;
        const int bottom = r.rows - top;
        lay(*node.first, {r.row, r.col, top, r.cols}, out, separators);
        lay(*node.second, {r.row + top, r.col, bottom, r.cols}, out, separators);
    }
}

}

Layout::Layout() : root_(makeLeaf(0)) {}

int Layout::split(int id, bool sideBySide) {
    Slot* slot = find(root_, id);
    if (slot == nullptr) return -1;

    const int fresh = nextId_++;
    auto node = std::make_unique<LayoutNode>();
    node->leaf = false;
    node->sideBySide = sideBySide;
    node->first = std::move(*slot);
    node->second = makeLeaf(fresh);
    *slot = std::move(node);
    return fresh;
}

bool Layout::close(int id) {
    return closeIn(root_, id);
}

void Layout::only(int id) {
    root_ = makeLeaf(id);
}

std::size_t Layout::count() const {
    return order().size();
}

std::vector<int> Layout::order() const {
    std::vector<int> ids;
    collect(*root_, ids);
    return ids;
}

std::vector<Placement> Layout::place(Rect area, std::vector<Rect>* separators) const {
    std::vector<Placement> placed;
    lay(*root_, area, placed, separators);
    return placed;
}

std::optional<Rect> Layout::rectOf(Rect area, int id) const {
    for (const Placement& p : place(area)) {
        if (p.id == id) return p.rect;
    }
    return std::nullopt;
}

std::optional<int> Layout::neighbor(Rect area, int id, Direction direction) const {
    const std::vector<Placement> all = place(area);

    const Rect* cur = nullptr;
    for (const Placement& p : all) {
        if (p.id == id) cur = &p.rect;
    }
    if (cur == nullptr) return std::nullopt;

    std::optional<int> best;
    int bestDistance = 0;

    for (const Placement& p : all) {
        if (p.id == id) continue;
        const Rect& c = p.rect;
        const bool rowsOverlap = c.row < cur->row + cur->rows && cur->row < c.row + c.rows;
        const bool colsOverlap = c.col < cur->col + cur->cols && cur->col < c.col + c.cols;

        bool adjacent = false;
        int distance = 0;
        switch (direction) {
            case Direction::Left:
                adjacent = rowsOverlap && c.col + c.cols + 1 == cur->col;
                distance = std::abs(c.row - cur->row);
                break;
            case Direction::Right:
                adjacent = rowsOverlap && cur->col + cur->cols + 1 == c.col;
                distance = std::abs(c.row - cur->row);
                break;
            case Direction::Up:
                adjacent = colsOverlap && c.row + c.rows == cur->row;
                distance = std::abs(c.col - cur->col);
                break;
            case Direction::Down:
                adjacent = colsOverlap && cur->row + cur->rows == c.row;
                distance = std::abs(c.col - cur->col);
                break;
        }

        if (adjacent && (!best || distance < bestDistance)) {
            best = p.id;
            bestDistance = distance;
        }
    }
    return best;
}

}