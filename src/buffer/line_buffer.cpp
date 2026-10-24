#include "buffer/line_buffer.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

namespace qalam {
    namespace {
        constexpr std::size_t kTarget = 256;
        constexpr std::size_t kMax = 512;

        std::vector<std::string> splitLines(const std::string &text) {
            std::vector<std::string> pieces;
            std::size_t begin = 0;
            while (true) {
                const std::size_t end = text.find('\n', begin);
                if (end == std::string::npos) {
                    pieces.push_back(text.substr(begin));
                    break;
                }
                pieces.push_back(text.substr(begin, end - begin));
                begin = end + 1;
            }
            return pieces;
        }
    }

    LineBuffer::LineBuffer() {
        chunks_.emplace_back(std::size_t{1});
        starts_.push_back(0);
        count_ = 1;
    }

    std::size_t LineBuffer::lineCount() const {
        return count_;
    }

    LineBuffer::Slot LineBuffer::locate(std::size_t row) const {
        const auto it = std::upper_bound(starts_.begin(), starts_.end(), row);
        const auto chunk = static_cast<std::size_t>(it - starts_.begin()) - 1;
        return {chunk, row - starts_[chunk]};
    }

    const std::string &LineBuffer::line(std::size_t index) const {
        const Slot s = locate(index);
        return chunks_[s.chunk][s.offset];
    }

    std::string &LineBuffer::lineRef(std::size_t row) {
        const Slot s = locate(row);
        return chunks_[s.chunk][s.offset];
    }

    void LineBuffer::rebuild(std::size_t from) {
        from = std::min(from, chunks_.size());
        starts_.resize(chunks_.size());

        std::size_t acc = from == 0 ? 0 : starts_[from - 1] + chunks_[from - 1].size();
        for (std::size_t i = from; i < chunks_.size(); ++i) {
            starts_[i] = acc;
            acc += chunks_[i].size();
        }
        count_ = acc;
    }

    void LineBuffer::assign(std::vector<std::string> lines) {
        if (lines.empty()) lines.emplace_back();

        chunks_.clear();
        for (std::size_t i = 0; i < lines.size(); i += kTarget) {
            const std::size_t end = std::min(i + kTarget, lines.size());
            chunks_.emplace_back(std::make_move_iterator(lines.begin() + static_cast<std::ptrdiff_t>(i)),
                                 std::make_move_iterator(lines.begin() + static_cast<std::ptrdiff_t>(end)));
        }
        rebuild(0);
    }

    void LineBuffer::split(std::size_t chunk) {
        std::vector<std::string> all = std::move(chunks_[chunk]);

        std::vector<std::vector<std::string> > parts;
        for (std::size_t i = 0; i < all.size(); i += kTarget) {
            const std::size_t end = std::min(i + kTarget, all.size());
            parts.emplace_back(std::make_move_iterator(all.begin() + static_cast<std::ptrdiff_t>(i)),
                               std::make_move_iterator(all.begin() + static_cast<std::ptrdiff_t>(end)));
        }

        chunks_[chunk] = std::move(parts.front());
        chunks_.insert(chunks_.begin() + static_cast<std::ptrdiff_t>(chunk) + 1,
                       std::make_move_iterator(parts.begin() + 1),
                       std::make_move_iterator(parts.end()));
    }

    void LineBuffer::insertRows(std::size_t row, std::vector<std::string> rows) {
        if (rows.empty()) return;

        std::size_t chunk = 0;
        std::size_t offset = 0;
        if (row >= count_) {
            chunk = chunks_.size() - 1;
            offset = chunks_[chunk].size();
        } else {
            const Slot s = locate(row);
            chunk = s.chunk;
            offset = s.offset;
        }

        std::vector<std::string> &target = chunks_[chunk];
        target.insert(target.begin() + static_cast<std::ptrdiff_t>(offset),
                      std::make_move_iterator(rows.begin()), std::make_move_iterator(rows.end()));
        if (target.size() > kMax) split(chunk);
        rebuild(chunk);
    }

    void LineBuffer::eraseRows(std::size_t row, std::size_t count) {
        if (count == 0) return;

        const Slot s = locate(row);
        const std::size_t first = s.chunk;
        std::size_t chunk = s.chunk;
        std::size_t offset = s.offset;
        std::size_t remaining = count;

        while (remaining > 0 && chunk < chunks_.size()) {
            std::vector<std::string> &target = chunks_[chunk];
            const std::size_t n = std::min(remaining, target.size() - offset);
            target.erase(target.begin() + static_cast<std::ptrdiff_t>(offset),
                         target.begin() + static_cast<std::ptrdiff_t>(offset + n));
            remaining -= n;
            offset = 0;
            if (target.empty()) {
                chunks_.erase(chunks_.begin() + static_cast<std::ptrdiff_t>(chunk));
            } else {
                ++chunk;
            }
        }
        rebuild(first);
    }

    std::string LineBuffer::text(Pos start, Pos end) const {
        if (start.row == end.row) {
            return line(start.row).substr(start.col, end.col - start.col);
        }

        std::string out = line(start.row).substr(start.col);
        for (std::size_t r = start.row + 1; r < end.row; ++r) {
            out += '\n';
            out += line(r);
        }
        out += '\n';
        out += line(end.row).substr(0, end.col);
        return out;
    }

    Edit LineBuffer::replace(Pos start, Pos end, const std::string &inserted) {
        Edit edit{start, text(start, end), inserted};

        if (start.row == end.row && inserted.find('\n') == std::string::npos) {
            lineRef(start.row).replace(start.col, end.col - start.col, inserted);
            return edit;
        }

        std::vector<std::string> pieces = splitLines(inserted);
        const std::string head = line(start.row).substr(0, start.col);
        const std::string tail = line(end.row).substr(end.col);
        pieces.front() = head + pieces.front();
        pieces.back() += tail;

        const std::size_t oldRows = end.row - start.row + 1;
        const std::size_t newRows = pieces.size();
        const std::size_t shared = std::min(oldRows, newRows);

        for (std::size_t i = 0; i < shared; ++i) lineRef(start.row + i) = std::move(pieces[i]);

        if (newRows > oldRows) {
            std::vector<std::string> extra(
                std::make_move_iterator(pieces.begin() + static_cast<std::ptrdiff_t>(shared)),
                std::make_move_iterator(pieces.end()));
            insertRows(start.row + oldRows, std::move(extra));
        } else if (oldRows > newRows) {
            eraseRows(start.row + newRows, oldRows - newRows);
        }
        return edit;
    }
}
