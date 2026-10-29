#pragma once

#include <string>
#include <termios.h>
#include <utility>

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
        Paste,
        Mouse
    };

    struct Key {
        KeyType type = KeyType::None;
        char ch = 0;
        std::string text;
        int x = 0;
        int y = 0;
        int button = 0;
        bool press = false;

        Key() = default;
        Key(KeyType t, char c = 0, std::string s = {}) : type(t), ch(c), text(std::move(s)) {}
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

    std::string keyName(const Key& key);
}
