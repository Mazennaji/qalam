#include <cctype>
#include <cstdio>
#include <exception>

#include "terminal/terminal.h"

int main() {
    try {
        qalam::RawMode raw;

        std::printf("qalam: raw mode on. Press keys to see their codes. Ctrl-Q quits.\r\n");
        std::fflush(stdout);

        while (true) {
            auto c = qalam::readByte();
            if (!c) continue;

            if (*c == 17) break;  // Ctrl-Q

            if (std::isprint(*c)) {
                std::printf("%3d ('%c')\r\n", *c, *c);
            } else {
                std::printf("%3d\r\n", *c);
            }
            std::fflush(stdout);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
    return 0;
}