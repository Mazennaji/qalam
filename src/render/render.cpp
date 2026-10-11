#include "render/render.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "search/search.h"
#include "text/columns.h"

namespace qalam {
    namespace {
        std::string fitWidth(std::string left, const std::string &right, int width) {
            const auto w = static_cast<std::size_t>(std::max(width, 0));
            left = truncateToWidth(left, w);

            const auto leftWidth = static_cast<std::size_t>(displayWidth(left));
            const auto rightWidth = static_cast<std::size_t>(displayWidth(right));
            if (leftWidth + rightWidth <= w) {
                left.append(w - leftWidth - rightWidth, ' ');
                left += right;
            } else {
                left.append(w - leftWidth, ' ');
            }
            return left;
        }

        std::string moveTo(int row, int col) {
            return "\x1b[" + std::to_string(row) + ";" + std::to_string(col) + "H";
        }

        std::size_t appendWelcome(std::string &out, int cols) {
            const std::size_t before = out.size();

            std::string text = "qalam -- the pen for your terminal";
            const auto w = static_cast<std::size_t>(std::max(cols, 0));
            if (text.size() > w) text.resize(w);

            int padding = (cols - static_cast<int>(text.size())) / 2;
            if (padding > 0) {
                out += '~';
                --padding;
            }
            out.append(static_cast<std::size_t>(std::max(padding, 0)), ' ');
            out += text;
            return out.size() - before;
        }

        std::string gutterText(std::size_t row, std::size_t cursor, bool number, bool relative,
                               int gutter, bool first) {
            if (!first) return std::string(static_cast<std::size_t>(gutter), ' ');

            const auto digits = static_cast<std::size_t>(gutter - 1);
            std::size_t value = row + 1;
            if (relative) {
                const std::size_t distance = row > cursor ? row - cursor : cursor - row;
                value = distance == 0 && number ? row + 1 : distance;
            }

            std::string text = std::to_string(value);
            if (text.size() < digits) text.insert(0, digits - text.size(), ' ');

            const char *style = row == cursor ? "\x1b[1m" : "\x1b[2m";
            return std::string(style) + text + " \x1b[m";
        }


        std::size_t appendTextRow(std::string &out, const std::string &line, std::size_t row,
                                  int colOffset, int cols, const std::string &pattern,
                                  const std::optional<Selection> &selection, const Syntax *syntax,
                                  const std::vector<Kind> *kinds) {
            const std::vector<std::string> cells = displayCells(line);

            const bool selected = selection && row >= selection->start.row && row <= selection->end.row;
            if (cells.empty()) {
                if (selected) {
                    out += "\x1b[7m \x1b[m";
                    return 1;
                }
                return 0;
            }

            const auto start = static_cast<std::size_t>(std::max(colOffset, 0));
            if (start >= cells.size()) return 0;
            const std::size_t end =
                    std::min(cells.size(), start + static_cast<std::size_t>(std::max(cols, 0)));

            std::size_t selFrom = 0;
            std::size_t selTo = 0;
            if (selected) {
                if (selection->block) {
                    selFrom = selection->left;
                    selTo = selection->right + 1;
                } else if (selection->linewise) {
                    selTo = cells.size();
                } else {
                    if (row == selection->start.row) {
                        selFrom = static_cast<std::size_t>(displayColumn(line, selection->start.col));
                    }
                    selTo = row == selection->end.row
                                ? static_cast<std::size_t>(displayColumn(line, selection->end.col + 1))
                                : cells.size();
                }
            }

            std::vector<bool> marked;
            if (!pattern.empty()) {
                marked.assign(cells.size(), false);
                for (const Span &span: findInLine(line, pattern)) {
                    const auto from = static_cast<std::size_t>(displayColumn(line, span.first));
                    const auto to = static_cast<std::size_t>(displayColumn(line, span.second));
                    for (std::size_t i = from; i < to && i < marked.size(); ++i) marked[i] = true;
                }
            }

            std::vector<std::size_t> sources;
            if (kinds != nullptr && syntax != nullptr) sources = cellSources(line);

            int current = 0;
            for (std::size_t i = start; i < end; ++i) {
                int want = 0;
                if (i >= selFrom && i < selTo) {
                    want = 2;
                } else if (!marked.empty() && marked[i]) {
                    want = 1;
                } else if (!sources.empty() && i < sources.size() && sources[i] < kinds->size()) {
                    const Kind kind = (*kinds)[sources[i]];
                    if (kind != Kind::Normal) want = 2 + static_cast<int>(kind);
                }

                if (want != current) {
                    out += "\x1b[m";
                    if (want == 1) {
                        out += "\x1b[30;43m";
                    } else if (want == 2) {
                        out += "\x1b[7m";
                    } else if (want >= 3) {
                        out += syntax->sgr(static_cast<Kind>(want - 2));
                    }
                    current = want;
                }

                const std::string &glyph = cells[i];
                if (glyph.empty()) {
                    if (i == start) out += ' ';
                    continue;
                }
                const bool cut = i + 1 == end && i + 1 < cells.size() && cells[i + 1].empty();
                out += cut ? std::string(" ") : glyph;
            }
            if (current != 0) out += "\x1b[m";
            return end - start;
        }
    }

    int gutterWidth(bool show, std::size_t lineCount, int cols) {
        if (!show) return 0;
        int digits = 3;
        for (std::size_t n = lineCount; n >= 1000; n /= 10) ++digits;
        const int width = digits + 1;
        return width < cols ? width : 0;
    }

    std::string renderFrame(Size size, const ViewState &view) {
        std::string out;
        out.reserve(static_cast<std::size_t>(size.rows) * static_cast<std::size_t>(size.cols) * 2);

        out += "\x1b[?25l";
        out += "\x1b[m";

        int cursorRow = 1;
        int cursorCol = 1;

        for (const WindowView &w: view.windows) {
            if (w.rect.rows < 2 || w.rect.cols < 1) continue;

            const int textRows = w.rect.rows - 1;
            const auto width = static_cast<std::size_t>(w.rect.cols);
            const std::size_t lineCount = w.buffer != nullptr ? w.buffer->lineCount() : 0;
            const bool highlight = w.syntax != nullptr && w.syntax->active();

            for (const WindowView &w: view.windows) {
                if (w.rect.rows < 2 || w.rect.cols < 1) continue;

                const int textRows = w.rect.rows - 1;
                const std::size_t lineCount = w.buffer != nullptr ? w.buffer->lineCount() : 0;
                const bool highlight = w.syntax != nullptr && w.syntax->active();
                const bool numbers = w.number || w.relative;
                const int gutter = gutterWidth(numbers, lineCount, w.rect.cols);
                const int textCols = w.rect.cols - gutter;
                const auto width = static_cast<std::size_t>(textCols);

                int y = 0;
                std::size_t row = w.rowOffset;
                while (y < textRows) {
                    if (row >= lineCount) {
                        out += moveTo(w.rect.row + y + 1, w.rect.col + 1);
                        out.append(static_cast<std::size_t>(gutter), ' ');
                        std::size_t used = 0;
                        if (w.showWelcome && y == textRows / 3) {
                            used = appendWelcome(out, textCols);
                        } else {
                            out += '~';
                            used = 1;
                        }
                        if (used < width) out.append(width - used, ' ');
                        ++y;
                        continue;
                    }

                    const std::string &line = w.buffer->line(row);
                    const int segments = w.wrap ? wrapHeight(line, textCols) : 1;
                    const std::vector<Kind> *kinds = nullptr;
                    if (highlight) kinds = &w.syntax->kinds(*w.buffer, row);

                    for (int seg = 0; seg < segments && y < textRows; ++seg, ++y) {
                        out += moveTo(w.rect.row + y + 1, w.rect.col + 1);
                        if (gutter > 0) {
                            out += gutterText(row, w.line, w.number, w.relative, gutter, seg == 0);
                        }

                        const int offset = w.wrap ? seg * textCols : w.colOffset;
                        const std::size_t used =
                                appendTextRow(out, line, row, offset, textCols, view.searchPattern,
                                              w.selection, w.syntax, kinds);
                        if (used < width) out.append(width - used, ' ');
                    }
                    ++row;
                }

                const std::string name = w.fileName.empty() ? "[No Name]" : w.fileName;
                const std::string flag = w.modified ? " [+]" : "";
                const std::string left =
                        w.active ? " " + w.mode + " | " + name + flag : " " + name + flag;
                const std::string right =
                        "Ln " + std::to_string(w.line + 1) + ", Col " + std::to_string(w.col + 1) + " ";

                out += moveTo(w.rect.row + textRows + 1, w.rect.col + 1);
                out += w.active ? "\x1b[7m" : "\x1b[7;2m";
                out += fitWidth(left, right, w.rect.cols);
                out += "\x1b[m";

                if (w.active) {
                    const int r = std::clamp(w.cursorRow, 0, textRows - 1);
                    const int c = std::clamp(w.cursorCol, 0, w.rect.cols - 1);
                    cursorRow = w.rect.row + r + 1;
                    cursorCol = w.rect.col + c + 1;
                }
            }

            const std::string name = w.fileName.empty() ? "[No Name]" : w.fileName;
            const std::string flag = w.modified ? " [+]" : "";
            const std::string left =
                    w.active ? " " + w.mode + " | " + name + flag : " " + name + flag;
            const std::string right =
                    "Ln " + std::to_string(w.line + 1) + ", Col " + std::to_string(w.col + 1) + " ";

            out += moveTo(w.rect.row + textRows + 1, w.rect.col + 1);
            out += w.active ? "\x1b[7m" : "\x1b[7;2m";
            out += fitWidth(left, right, w.rect.cols);
            out += "\x1b[m";

            if (w.active) {
                const int row = std::clamp(w.cursorRow, 0, textRows - 1);
                const int col = std::clamp(w.cursorCol, 0, w.rect.cols - 1);
                cursorRow = w.rect.row + row + 1;
                cursorCol = w.rect.col + col + 1;
            }
        }

        for (const Rect &s: view.separators) {
            for (int r = 0; r < s.rows; ++r) {
                out += moveTo(s.row + r + 1, s.col + 1);
                out += "\xe2\x94\x82";
            }
        }

        const auto w = static_cast<std::size_t>(std::max(size.cols, 0));
        const std::string msg = truncateToWidth(view.message, w);
        out += moveTo(size.rows, 1);
        out += msg;
        out += "\x1b[K";

        if (view.commandLine) {
            cursorRow = size.rows;
            cursorCol = std::min(displayWidth(msg) + 1, std::max(size.cols, 1));
        }

        out += moveTo(cursorRow, cursorCol);
        out += "\x1b[?25h";
        return out;
    }
}
