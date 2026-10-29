#include "terminal/terminal.h"

#include <cerrno>
#include <stdexcept>
#include <sys/ioctl.h>
#include <unistd.h>

#include "terminal/decoder.h"

namespace qalam {
    RawMode::RawMode() {
        if (!isatty(STDIN_FILENO)) throw std::runtime_error("qalam needs an interactive terminal");
        if (tcgetattr(STDIN_FILENO, &original_) == -1) throw std::runtime_error("tcgetattr failed");

        termios raw = original_;
        raw.c_iflag &= ~static_cast<tcflag_t>(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~static_cast<tcflag_t>(OPOST);
        raw.c_cflag |= CS8;
        raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 1;

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) throw std::runtime_error("tcsetattr failed");
    }

    RawMode::~RawMode() {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
    }

    AlternateScreen::AlternateScreen() {
        writeAll("\x1b[?1049h\x1b[?2004h\x1b[?1000h\x1b[?1002h\x1b[?1006h\x1b[2J\x1b[H");
    }

    AlternateScreen::~AlternateScreen() {
        writeAll("\x1b[?1006l\x1b[?1002l\x1b[?1000l\x1b[?2004l\x1b[?1049l");
    }

    Key readKey() {
        static std::string pending;

        for (;;) {
            if (!pending.empty()) {
                const Decoded d = decodeKey(pending, false);
                if (!d.needMore) {
                    pending.erase(0, d.used);
                    if (d.key.type == KeyType::None) continue;
                    return d.key;
                }
            }

            char chunk[4096];
            const ssize_t n = read(STDIN_FILENO, chunk, sizeof chunk);

            if (n > 0) {
                pending.append(chunk, static_cast<std::size_t>(n));
                continue;
            }

            if (n < 0 && errno != EAGAIN && errno != EINTR) return Key{};

            if (pending.empty()) return Key{};

            const Decoded d = decodeKey(pending, true);
            pending.erase(0, d.used);
            if (d.key.type == KeyType::None) continue;
            return d.key;
        }
    }

    Size windowSize() {
        winsize ws{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 || ws.ws_row == 0) {
            return Size{};
        }
        Size size;
        size.rows = ws.ws_row;
        size.cols = ws.ws_col;
        return size;
    }

    void writeAll(const std::string &data) {
        std::size_t done = 0;
        while (done < data.size()) {
            const ssize_t n = write(STDOUT_FILENO, data.data() + done, data.size() - done);
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                return;
            }
            done += static_cast<std::size_t>(n);
        }
    }

    std::string keyName(const Key& key) {
        switch (key.type) {
            case KeyType::Escape: return "Esc";
            case KeyType::Enter: return "CR";
            case KeyType::Tab: return "Tab";
            case KeyType::Backspace: return "BS";
            case KeyType::Delete: return "Del";
            case KeyType::Up: return "Up";
            case KeyType::Down: return "Down";
            case KeyType::Left: return "Left";
            case KeyType::Right: return "Right";
            case KeyType::Home: return "Home";
            case KeyType::End: return "End";
            case KeyType::PageUp: return "PageUp";
            case KeyType::PageDown: return "PageDown";
            case KeyType::Paste: return "Paste";
            case KeyType::Ctrl: return std::string("C-") + key.ch;
            case KeyType::Char: return std::string(1, key.ch);
            case KeyType::Text: return key.text;
            case KeyType::None: return "None";
            case KeyType::Mouse: return "Mouse";
        }
        return "";
    }
}
