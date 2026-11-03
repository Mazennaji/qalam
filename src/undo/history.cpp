#include "undo/history.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <sstream>

namespace qalam {
    namespace {
        constexpr std::size_t kLost = std::numeric_limits<std::size_t>::max();

        Pos restoredStart(const Edit &edit) {
            if (!edit.removed.empty() && edit.removed.front() == '\n') {
                return {edit.start.row + 1, 0};
            }
            return edit.start;
        }
        struct Reader {
                const std::string& s;
                std::size_t pos = 0;

                bool line(std::string& out) {
                    const std::size_t end = s.find('\n', pos);
                    if (end == std::string::npos) return false;
                    out = s.substr(pos, end - pos);
                    pos = end + 1;
                    return true;
                }

                bool take(std::size_t n, std::string& out) {
                    if (n > s.size() - pos) return false;
                    out = s.substr(pos, n);
                    pos += n;
                    return true;
                }
            };

        bool fields(const std::string& line, char tag, std::size_t count, std::size_t* values) {
            if (line.empty() || line[0] != tag || line.find('-') != std::string::npos) return false;
            std::istringstream in(line.substr(1));
            for (std::size_t i = 0; i < count; ++i) {
                unsigned long long v = 0;
                if (!(in >> v)) return false;
                values[i] = static_cast<std::size_t>(v);
            }
            std::string extra;
            return !(in >> extra);
        }
    }

    std::uint64_t fnv1a(const std::string& data) {
        std::uint64_t h = 1469598103934665603ULL;
        for (char c : data) {
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ULL;
        }
        return h;
    }

    std::string UndoHistory::serialize() const {
        std::string out;
        auto put = [&](char tag, const std::vector<Group>& groups) {
            out += std::string(1, tag) + " " + std::to_string(groups.size()) + "\n";
            for (const Group& g : groups) {
                out += "G " + std::to_string(g.size()) + "\n";
                for (const Edit& e : g) {
                    out += "E " + std::to_string(e.start.row) + " " + std::to_string(e.start.col) +
                           " " + std::to_string(e.removed.size()) + " " +
                           std::to_string(e.inserted.size()) + "\n";
                    out += e.removed;
                    out += e.inserted;
                    out += '\n';
                }
            }
        };
        put('U', undo_);
        put('R', redo_);
        return out;
    }

    bool UndoHistory::deserialize(const std::string& data) {
        Reader r{data};
        std::vector<Group> parsed[2];

        for (int k = 0; k < 2; ++k) {
            std::string line;
            std::size_t head[1] = {0};
            if (!r.line(line) || !fields(line, k == 0 ? 'U' : 'R', 1, head)) return false;
            if (head[0] > 1000000) return false;

            for (std::size_t i = 0; i < head[0]; ++i) {
                std::size_t group[1] = {0};
                if (!r.line(line) || !fields(line, 'G', 1, group) || group[0] > 1000000) return false;

                Group g;
                for (std::size_t j = 0; j < group[0]; ++j) {
                    std::size_t e[4] = {0, 0, 0, 0};
                    if (!r.line(line) || !fields(line, 'E', 4, e)) return false;

                    Edit edit;
                    edit.start = {e[0], e[1]};
                    std::string tail;
                    if (!r.take(e[2], edit.removed) || !r.take(e[3], edit.inserted) ||
                        !r.take(1, tail) || tail != "\n") {
                        return false;
                    }
                    g.push_back(std::move(edit));
                }
                parsed[k].push_back(std::move(g));
            }
        }
        if (r.pos != data.size()) return false;

        undo_ = std::move(parsed[0]);
        redo_ = std::move(parsed[1]);
        open_.clear();
        savePoint_ = undo_.size();
        return true;
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

    std::optional<Pos> UndoHistory::undo(TextBuffer &buffer) {
        close();
        if (undo_.empty()) return std::nullopt;

        Group group = std::move(undo_.back());
        undo_.pop_back();

        Pos where = restoredStart(group.front());
        touched_ = group.front().start.row;
        for (auto it = group.rbegin(); it != group.rend(); ++it) {
            buffer.replace(it->start, endOf(it->start, it->inserted), it->removed);
            where = std::min(where, restoredStart(*it));
            touched_ = std::min(touched_, it->start.row);
        }

        redo_.push_back(std::move(group));
        return where;
    }

    std::optional<Pos> UndoHistory::redo(TextBuffer &buffer) {
        close();
        if (redo_.empty()) return std::nullopt;

        Group group = std::move(redo_.back());
        redo_.pop_back();

        Pos where = group.front().start;
        touched_ = group.front().start.row;
        for (const Edit& edit : group) {
            buffer.replace(edit.start, endOf(edit.start, edit.removed), edit.inserted);
            where = std::min(where, edit.start);
            touched_ = std::min(touched_, edit.start.row);
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

    std::size_t UndoHistory::touchedRow() const {
        return touched_;
    }

    void UndoHistory::clear() {
        undo_.clear();
        redo_.clear();
        open_.clear();
        savePoint_ = 0;
    }
}
