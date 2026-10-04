#include <algorithm>
#include <cstdio>
#include <exception>
#include <string>

#include "render/render.h"
#include "terminal/terminal.h"

using namespace qalam;

int main() {
    try {
        RawMode raw;
        AlternateScreen screen;

        int row = 0;
        int col = 0;
        std::string lastKey = "none";
        std::string message = "Ctrl-Q quits. Arrows, Home, End, PageUp, PageDown move the cursor.";

        while (true) {
            const Size size = windowSize();
            const int textRows = std::max(1, size.rows - 2);
            row = std::clamp(row, 0, textRows - 1);
            col = std::clamp(col, 0, size.cols - 1);

            ViewState view;
            view.mode = "NORMAL";
            view.status = "key: " + lastKey;
            view.message = message;
            view.cursorRow = row;
            view.cursorCol = col;
            writeAll(renderFrame(size, view));

            Key key;
            while ((key = readKey()).type == KeyType::None) {
                if (windowSize() != size) break;
            }
            if (key.type == KeyType::None) continue;

            lastKey = keyName(key);
            message.clear();

            if (key.type == KeyType::Ctrl && key.ch == 'q') break;

            switch (key.type) {
                case KeyType::Up: --row; break;
                case KeyType::Down: ++row; break;
                case KeyType::Left: --col; break;
                case KeyType::Right: ++col; break;
                case KeyType::Home: col = 0; break;
                case KeyType::End: col = size.cols - 1; break;
                case KeyType::PageUp: row = 0; break;
                case KeyType::PageDown: row = textRows - 1; break;
                default: break;
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return 0;
}