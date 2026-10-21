#include <algorithm>
#include <vector>

#include "check.h"
#include "window/layout.h"

using namespace qalam;

namespace {
    const Rect kArea{0, 0, 20, 80};

    std::optional<Rect> rect(const std::vector<Placement> &placed, int id) {
        for (const Placement &p: placed) {
            if (p.id == id) return p.rect;
        }
        return std::nullopt;
    }
}

TEST(layout_single_window_fills_area) {
    Layout layout;
    const std::vector<int> order = layout.order();
    CHECK_EQ(order.size(), std::size_t{1});
    const auto placed = layout.place(kArea, nullptr);
    CHECK_EQ(placed.size(), std::size_t{1});
    CHECK_EQ(placed[0].rect.rows, 20);
    CHECK_EQ(placed[0].rect.cols, 80);
}

TEST(layout_horizontal_split_stacks_windows) {
    Layout layout;
    const int first = layout.order().front();
    const int second = layout.split(first, false);
    CHECK(second >= 0);

    std::vector<Rect> separators;
    const auto placed = layout.place(kArea, &separators);
    CHECK_EQ(placed.size(), std::size_t{2});

    const auto a = rect(placed, first);
    const auto b = rect(placed, second);
    CHECK(a.has_value() && b.has_value());
    CHECK_EQ(a->cols, 80);
    CHECK_EQ(b->cols, 80);
    CHECK(a->rows + b->rows <= 20);
    CHECK(a->row != b->row);
}

TEST(layout_vertical_split_sits_side_by_side) {
    Layout layout;
    const int first = layout.order().front();
    const int second = layout.split(first, true);
    CHECK(second >= 0);

    std::vector<Rect> separators;
    const auto placed = layout.place(kArea, &separators);
    const auto a = rect(placed, first);
    const auto b = rect(placed, second);
    CHECK(a.has_value() && b.has_value());
    CHECK_EQ(a->rows, 20);
    CHECK_EQ(b->rows, 20);
    CHECK(a->cols + b->cols < 80);
    CHECK(!separators.empty());
}

TEST(layout_neighbors) {
    Layout layout;
    const int left = layout.order().front();
    const int right = layout.split(left, true);

    const auto toRight = layout.neighbor(kArea, left, Direction::Right);
    CHECK(toRight.has_value());
    CHECK_EQ(*toRight, right);

    const auto toLeft = layout.neighbor(kArea, right, Direction::Left);
    CHECK(toLeft.has_value());
    CHECK_EQ(*toLeft, left);

    CHECK(!layout.neighbor(kArea, left, Direction::Up).has_value());
    CHECK(!layout.neighbor(kArea, left, Direction::Left).has_value());
}

TEST(layout_close_returns_space) {
    Layout layout;
    const int first = layout.order().front();
    const int second = layout.split(first, false);
    layout.close(second);

    CHECK_EQ(layout.order().size(), std::size_t{1});
    const auto placed = layout.place(kArea, nullptr);
    CHECK_EQ(placed.size(), std::size_t{1});
    CHECK_EQ(placed[0].rect.rows, 20);
}

TEST(layout_only_keeps_one) {
    Layout layout;
    const int first = layout.order().front();
    const int second = layout.split(first, true);
    layout.split(second, false);
    CHECK_EQ(layout.order().size(), std::size_t{3});

    layout.only(second);
    CHECK_EQ(layout.order().size(), std::size_t{1});
    CHECK_EQ(layout.order().front(), second);
}

TEST(layout_order_is_stable_and_complete) {
    Layout layout;
    const int a = layout.order().front();
    const int b = layout.split(a, true);
    const int c = layout.split(b, false);

    const std::vector<int> order = layout.order();
    CHECK_EQ(order.size(), std::size_t{3});
    CHECK(std::find(order.begin(), order.end(), a) != order.end());
    CHECK(std::find(order.begin(), order.end(), b) != order.end());
    CHECK(std::find(order.begin(), order.end(), c) != order.end());
}

TEST(layout_rect_of_matches_place) {
    Layout layout;
    const int first = layout.order().front();
    const int second = layout.split(first, true);
    const auto placed = layout.place(kArea, nullptr);

    const auto direct = layout.rectOf(kArea, second);
    const auto viaPlace = rect(placed, second);
    CHECK(direct.has_value() && viaPlace.has_value());
    CHECK_EQ(direct->cols, viaPlace->cols);
    CHECK_EQ(direct->col, viaPlace->col);
}
