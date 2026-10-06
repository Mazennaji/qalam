#include <cstdio>
#include <exception>
#include <memory>

#include "buffer/line_buffer.h"
#include "config/config.h"
#include "editor/editor.h"
#include "render/render.h"
#include "terminal/terminal.h"

using namespace qalam;

int main(int argc, char** argv) {
    try {
        RawMode raw;
        AlternateScreen screen;

        Editor editor(std::make_unique<LineBuffer>());
        if (argc > 1) editor.open(argv[1]);
        editor.useConfig(defaultConfigPath());

        Size size = windowSize();
        editor.resize(size);

        while (!editor.shouldQuit()) {
            writeAll(renderFrame(size, editor.view()));

            Key key = readKey();
            while (key.type == KeyType::None && windowSize() == size) {
                if (editor.idle()) break;
                key = readKey();
            }

            const Size current = windowSize();
            if (current != size) {
                size = current;
                editor.resize(size);
            }
            if (key.type != KeyType::None) editor.handleKey(key);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return 0;
}