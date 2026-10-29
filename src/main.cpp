#include <cstdio>
#include <exception>
#include <memory>

#include "buffer/line_buffer.h"
#include "config/config.h"
#include "editor/editor.h"
#include "render/render.h"
#include "terminal/terminal.h"

using namespace qalam;

int main(int argc, char **argv) {
    try {
        RawMode raw;
        AlternateScreen screen;

        Editor editor(std::make_unique<LineBuffer>());
        for (int i = 1; i < argc; ++i) {
            editor.edit(argv[i]);
        }

        if (argc > 2) {
            editor.selectBuffer(0);
        }

        editor.useConfig(defaultConfigPath());
        editor.loadHistory(defaultHistoryPath());

        Size size = windowSize();
        editor.resize(size);

        bool mouseOn = false;

        while (!editor.shouldQuit()) {
            if (editor.mouseEnabled() != mouseOn) {
                mouseOn = editor.mouseEnabled();

                writeAll(mouseOn
                    ? "\x1b[?1000h\x1b[?1002h\x1b[?1006h"
                    : "\x1b[?1006l\x1b[?1002l\x1b[?1000l");
            }

            writeAll(renderFrame(size, editor.view()));

            Key key = readKey();

            while (key.type == KeyType::None && windowSize() == size) {
                if (editor.idle()) {
                    break;
                }
                key = readKey();
            }

            const Size current = windowSize();
            if (current != size) {
                size = current;
                editor.resize(size);
            }

            if (key.type != KeyType::None) {
                editor.handleKey(key);
            }
        }

        editor.saveHistory();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }

    return 0;
}
