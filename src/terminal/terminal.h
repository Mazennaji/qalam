#pragma once

#include <string>
#include <termios.h>

namespace qalam {
    enum class KeyType {
        None,
        Char,
        Text,
        Ctrl,
        Escape,
        Enter,
        Tab,
        Backspace,
        Delete,
        Up,
        Down,
        Left,
        Right,
        Home,
        End,
        PageUp,
        PageDown,
        Paste
    };

    struct Key {
        KeyType type = KeyType::None;
        char ch = 0;
        std::string text;
    };

    struct Size {
        int rows = 24;
        int cols = 80;

        bool operator==(const Size &) const = default;
    };

    class RawMode {
    public:
        RawMode();

        ~RawMode();

        RawMode(const RawMode &) = delete;

        RawMode &operator=(const RawMode &) = delete;

    private:
        termios original_{};
    };

    class AlternateScreen {
    public:
        AlternateScreen();

        ~AlternateScreen();

        AlternateScreen(const AlternateScreen &) = delete;

        AlternateScreen &operator=(const AlternateScreen &) = delete;
    };

    Key readKey();

    Size windowSize();

    void writeAll(const std::string &data);
}
