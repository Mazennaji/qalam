#include "terminal/terminal.h"

#include <unistd.h>

#include <stdexcept>

namespace qalam {

    RawMode::RawMode() {
        if (!isatty(STDIN_FILENO)) {
            throw std::runtime_error(
                "qalam: stdin is not a terminal "
                "(in CLion, enable 'Emulate terminal in output console')");
        }
        if (tcgetattr(STDIN_FILENO, &orig_) == -1) {
            throw std::runtime_error("qalam: tcgetattr failed");
        }

        termios raw = orig_;
        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= CS8;
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 1;

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
            throw std::runtime_error("qalam: tcsetattr failed");
        }
    }

    RawMode::~RawMode() {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_);
    }

    std::optional<unsigned char> readByte() {
        unsigned char c = 0;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n == 1) return c;
        return std::nullopt;
    }

}