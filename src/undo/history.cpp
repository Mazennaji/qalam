#include "undo/history.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace qalam {
namespace {

constexpr std::size_t kLost = std::numeric_limits<std::size_t>::max();

Pos restoredStart(const Edit& edit) {
    if (!edit.removed.empty() && edit.removed.front() == '\n') {
        return {edit.start.row + 1, 0};
    }
    return edit.start;
}

}

void UndoHistory::record(Edit edit) {
    if (edit.removed.empty() && edit.inserted.empty()) return;
    if (open_.empty()) {
        redo_.clear();
        if (savePoint_ > undo_.size()) savePoint_ = kLost;
    }
    open_.push_back(std::move(edit));
}

void UndoHistory::close() {
    if (open_.empty()) return;
    undo_.push_back(std::move(open_));
    open_.clear();
}

std::optional<Pos> UndoHistory::undo(TextBuffer& buffer) {
    close();
    if (undo_.empty()) return std::nullopt;

    Group group = std::move(undo_.back());
    undo_.pop_back();

    Pos where = restoredStart(group.front());
    for (auto it = group.rbegin(); it != group.rend(); ++it) {
        buffer.replace(it->start, endOf(it->start, it->inserted), it->removed);
        where = std::min(where, restoredStart(*it));
    }

    redo_.push_back(std::move(group));
    return where;
}

std::optional<Pos> UndoHistory::redo(TextBuffer& buffer) {
    close();
    if (redo_.empty()) return std::nullopt;

    Group group = std::move(redo_.back());
    redo_.pop_back();

    Pos where = group.front().start;
    for (const Edit& edit : group) {
        buffer.replace(edit.start, endOf(edit.start, edit.removed), edit.inserted);
        where = std::min(where, edit.start);
    }

    undo_.push_back(std::move(group));
    return where;
}

void UndoHistory::markSaved() {
    close();
    savePoint_ = undo_.size();
}

bool UndoHistory::modified() const {
    return !open_.empty() || undo_.size() != savePoint_;
}

void UndoHistory::clear() {
    undo_.clear();
    redo_.clear();
    open_.clear();
    savePoint_ = 0;
}

}