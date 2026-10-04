#include "render/render.h"

#include <algorithm>
#include <cstddef>
#include <utility>

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

}

std::string renderFrame(Size size, const ViewState& view) {
    std::string out;
    out.reserve(static_cast<std::size_t>(size.rows) * static_cast<std::size_t>(size.cols));

    out += "\x1b[?25l";
    out += "\x1b[H";

    const int textRows = std::max(1, size.rows - 2);
    for (int y = 0; y < textRows; ++y) {
        if (y == textRows / 3) {
            appendWelcome(out, size.cols);
        } else {
            out += '~';
        }
        out += "\x1b[K\r\n";
    }

    const std::string left = " " + view.mode + " | " + view.status;
    const std::string right = "Ln " + std::to_string(view.cursorRow + 1) + ", Col " +
                              std::to_string(view.cursorCol + 1) + " ";
    out += "\x1b[7m";
    out += fitWidth(left, right, size.cols);
    out += "\x1b[m\r\n";

    std::string msg = view.message;
    const auto w = static_cast<std::size_t>(std::max(size.cols, 0));
    if (msg.size() > w) msg.resize(w);
    out += msg;
    out += "\x1b[K";

    out += "\x1b[" + std::to_string(view.cursorRow + 1) + ";" + std::to_string(view.cursorCol + 1) +
           "H";
    out += "\x1b[?25h";
    return out;
}

}