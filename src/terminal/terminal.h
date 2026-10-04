#pragma once

#include <termios.h>

#include <optional>
#include <string>

namespace qalam {

    class RawMode {
    public:
        RawMode();
        ~RawMode();

        RawMode(const RawMode&) = delete;
        RawMode& operator=(const RawMode&) = delete;

    private:
        termios orig_{};
    };

    class AlternateScreen {
    public:
        AlternateScreen();
        ~AlternateScreen();

        AlternateScreen(const AlternateScreen&) = delete;
        AlternateScreen& operator=(const AlternateScreen&) = delete;
    };

    std::optional<unsigned char> readByte();

    enum class KeyType {
        None,
        Char,
        Ctrl,
        Enter,
        Tab,
        Backspace,
        Escape,
        Up,
        Down,
        Left,
        Right,
        Home,
        End,
        PageUp,
        PageDown,
        Delete,
        Unknown,
    };

    struct Key {
        KeyType type = KeyType::None;
        char ch = 0;
    };

    Key readKey();

    std::string keyName(const Key& key);

    struct Size {
        int rows;
        int cols;
        bool operator==(const Size&) const = default;
    };

    Size windowSize();

    void writeAll(const std::string& s);

}