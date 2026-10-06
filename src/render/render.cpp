#include "render/render.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "search/search.h"
#include "text/columns.h"

namespace qalam {
namespace {

std::string fitWidth(std::string left, const std::string& right, int width) {
    const auto w = static_cast<std::size_t>(std::max(width, 0));
    if (left.size() > w) left.resize(w);

    std::string line = std::move(left);
    if (line.size() + right.size() <= w) {
        line.append(w - line.size() - right.size(), ' ');
        line += right;
    } else {
        line.append(w - line.size(), ' ');
    }
    return line;
}

void appendWelcome(std::string& out, int cols) {
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
}

void appendTextRow(std::string& out, const std::string& line, std::size_t row, int colOffset,
                   int cols, const std::string& pattern,
                   const std::optional<Selection>& selection) {
    const std::string text = expandTabs(line);

    const bool selected = selection && row >= selection->start.row && row <= selection->end.row;
    if (text.empty()) {
        if (selected) out += "\x1b[7m \x1b[m";
        return;
    }

    const auto start = static_cast<std::size_t>(std::max(colOffset, 0));
    if (start >= text.size()) return;
    const std::size_t end =
        std::min(text.size(), start + static_cast<std::size_t>(std::max(cols, 0)));

    std::size_t selFrom = 0;
    std::size_t selTo = 0;
    if (selected) {
        if (selection->linewise) {
            selTo = text.size();
        } else {
            if (row == selection->start.row) {
                selFrom = static_cast<std::size_t>(displayColumn(line, selection->start.col));
            }
            selTo = row == selection->end.row
                        ? static_cast<std::size_t>(displayColumn(line, selection->end.col + 1))
                        : text.size();
        }
    }

    std::vector<bool> marked;
    if (!pattern.empty()) {
        marked.assign(text.size(), false);
        for (const Span& span : findInLine(line, pattern)) {
            const auto from = static_cast<std::size_t>(displayColumn(line, span.first));
            const auto to = static_cast<std::size_t>(displayColumn(line, span.second));
            for (std::size_t i = from; i < to && i < marked.size(); ++i) marked[i] = true;
        }
    }

    int current = 0;
    for (std::size_t i = start; i < end; ++i) {
        int want = 0;
        if (i >= selFrom && i < selTo) {
            want = 2;
        } else if (!marked.empty() && marked[i]) {
            want = 1;
        }
        if (want != current) {
            out += "\x1b[m";
            if (want == 1) out += "\x1b[30;43m";
            if (want == 2) out += "\x1b[7m";
            current = want;
        }
        out += text[i];
    }
    if (current != 0) out += "\x1b[m";
}

}

std::string renderFrame(Size size, const ViewState& view) {
    std::string out;
    out.reserve(static_cast<std::size_t>(size.rows) * static_cast<std::size_t>(size.cols));

    out += "\x1b[?25l";
    out += "\x1b[H";

    const int textRows = std::max(1, size.rows - 2);
    const std::size_t lineCount = view.buffer != nullptr ? view.buffer->lineCount() : 0;

    for (int y = 0; y < textRows; ++y) {
        const std::size_t row = view.rowOffset + static_cast<std::size_t>(y);
        if (row < lineCount) {
            appendTextRow(out, view.buffer->line(row), row, view.colOffset, size.cols,
                          view.searchPattern, view.selection);
        } else if (view.showWelcome && y == textRows / 3) {
            appendWelcome(out, size.cols);
        } else {
            out += '~';
        }
        out += "\x1b[K\r\n";
    }

    const std::string name = view.fileName.empty() ? "[No Name]" : view.fileName;
    const std::string left = " " + view.mode + " | " + name + (view.modified ? " [+]" : "");
    const std::string right =
        "Ln " + std::to_string(view.line + 1) + ", Col " + std::to_string(view.col + 1) + " ";
    out += "\x1b[7m";
    out += fitWidth(left, right, size.cols);
    out += "\x1b[m\r\n";

    std::string msg = view.message;
    const auto w = static_cast<std::size_t>(std::max(size.cols, 0));
    if (msg.size() > w) msg.resize(w);
    out += msg;
    out += "\x1b[K";

    int cursorRow = view.cursorRow + 1;
    int cursorCol = view.cursorCol + 1;
    if (view.commandLine) {
        cursorRow = size.rows;
        cursorCol = std::min(static_cast<int>(msg.size()) + 1, std::max(size.cols, 1));
    }

    out += "\x1b[" + std::to_string(cursorRow) + ";" + std::to_string(cursorCol) + "H";
    out += "\x1b[?25h";
    return out;
}

}