#include "terminal/terminal.h"

#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
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

void writeAll(const std::string& s) {
    const char* p = s.data();
    std::size_t left = s.size();
    while (left > 0) {
        ssize_t n = write(STDOUT_FILENO, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return;
        }
        p += n;
        left -= static_cast<std::size_t>(n);
    }
}

AlternateScreen::AlternateScreen() {
    writeAll("\x1b[?1049h\x1b[H");
}

AlternateScreen::~AlternateScreen() {
    writeAll("\x1b[?25h\x1b[?1049l");
}

Size windowSize() {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 || ws.ws_row == 0) {
        return {24, 80};
    }
    return {ws.ws_row, ws.ws_col};
}

std::optional<unsigned char> readByte() {
    unsigned char c = 0;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1) return c;
    return std::nullopt;
}

namespace {

Key decodeCsi() {
    std::string params;
    unsigned char last = 0;

    while (true) {
        auto b = readByte();
        if (!b) return {KeyType::Unknown, 0};
        if (*b >= 0x40 && *b <= 0x7E) {
            last = *b;
            break;
        }
        params.push_back(static_cast<char>(*b));
        if (params.size() > 16) return {KeyType::Unknown, 0};
    }

    switch (last) {
        case 'A': return {KeyType::Up, 0};
        case 'B': return {KeyType::Down, 0};
        case 'C': return {KeyType::Right, 0};
        case 'D': return {KeyType::Left, 0};
        case 'H': return {KeyType::Home, 0};
        case 'F': return {KeyType::End, 0};
        case '~':
            switch (std::atoi(params.c_str())) {
                case 1:
                case 7: return {KeyType::Home, 0};
                case 3: return {KeyType::Delete, 0};
                case 4:
                case 8: return {KeyType::End, 0};
                case 5: return {KeyType::PageUp, 0};
                case 6: return {KeyType::PageDown, 0};
                default: break;
            }
            break;
        default: break;
    }
    return {KeyType::Unknown, 0};
}

Key decodeEscape() {
    auto b1 = readByte();
    if (!b1) return {KeyType::Escape, 0};

    if (*b1 == '[') return decodeCsi();

    if (*b1 == 'O') {
        auto b2 = readByte();
        if (b2 && *b2 == 'H') return {KeyType::Home, 0};
        if (b2 && *b2 == 'F') return {KeyType::End, 0};
    }
    return {KeyType::Unknown, 0};
}

Key decodeUtf8(unsigned char lead) {
    const int extra = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : 1;
    std::string text(1, static_cast<char>(lead));

    for (int i = 0; i < extra; ++i) {
        auto b = readByte();
        if (!b || (*b & 0xC0) != 0x80) return {KeyType::Unknown, 0};
        text.push_back(static_cast<char>(*b));
    }
    return {KeyType::Text, 0, text};
}

}

Key readKey() {
    auto b = readByte();
    if (!b) return {};

    const unsigned char c = *b;
    if (c == 27) return decodeEscape();
    if (c == 13) return {KeyType::Enter, 0};
    if (c == 9) return {KeyType::Tab, 0};
    if (c == 127 || c == 8) return {KeyType::Backspace, 0};
    if (c >= 1 && c <= 26) return {KeyType::Ctrl, static_cast<char>('a' + c - 1)};
    if (c >= 32 && c < 127) return {KeyType::Char, static_cast<char>(c)};
    if (c >= 0xC2 && c <= 0xF4) return decodeUtf8(c);
    return {KeyType::Unknown, 0};
}

std::string keyName(const Key& key) {
    switch (key.type) {
        case KeyType::None: return "none";
        case KeyType::Char: return std::string("'") + key.ch + "'";
        case KeyType::Text: return "'" + key.text + "'";
        case KeyType::Ctrl: return std::string("Ctrl-") + key.ch;
        case KeyType::Enter: return "Enter";
        case KeyType::Tab: return "Tab";
        case KeyType::Backspace: return "Backspace";
        case KeyType::Escape: return "Esc";
        case KeyType::Up: return "Up";
        case KeyType::Down: return "Down";
        case KeyType::Left: return "Left";
        case KeyType::Right: return "Right";
        case KeyType::Home: return "Home";
        case KeyType::End: return "End";
        case KeyType::PageUp: return "PageUp";
        case KeyType::PageDown: return "PageDown";
        case KeyType::Delete: return "Delete";
        case KeyType::Unknown: return "unknown";
    }
    return "?";
}

}