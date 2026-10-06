#include "input/key_notation.h"

#include <cctype>

namespace qalam {
namespace {

std::string lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::optional<Key> namedKey(const std::string& name) {
    if (name == "esc") return Key{KeyType::Escape, 0};
    if (name == "cr" || name == "enter" || name == "return") return Key{KeyType::Enter, 0};
    if (name == "tab") return Key{KeyType::Tab, 0};
    if (name == "bs" || name == "backspace") return Key{KeyType::Backspace, 0};
    if (name == "space") return Key{KeyType::Char, ' '};
    if (name == "lt") return Key{KeyType::Char, '<'};
    if (name == "gt") return Key{KeyType::Char, '>'};
    if (name == "up") return Key{KeyType::Up, 0};
    if (name == "down") return Key{KeyType::Down, 0};
    if (name == "left") return Key{KeyType::Left, 0};
    if (name == "right") return Key{KeyType::Right, 0};
    if (name == "home") return Key{KeyType::Home, 0};
    if (name == "end") return Key{KeyType::End, 0};
    if (name == "pageup") return Key{KeyType::PageUp, 0};
    if (name == "pagedown") return Key{KeyType::PageDown, 0};
    if (name == "del" || name == "delete") return Key{KeyType::Delete, 0};
    if (name.size() == 3 && name[0] == 'c' && name[1] == '-' &&
        std::isalpha(static_cast<unsigned char>(name[2]))) {
        return Key{KeyType::Ctrl, name[2]};
    }
    return std::nullopt;
}

}

std::optional<std::vector<Key>> parseKeys(const std::string& text, std::string& error) {
    std::vector<Key> keys;
    std::size_t i = 0;

    while (i < text.size()) {
        if (text[i] == '<') {
            const std::size_t close = text.find('>', i + 1);
            if (close != std::string::npos && close > i + 1) {
                const std::string name = lower(text.substr(i + 1, close - i - 1));
                if (name == "nop") {
                    i = close + 1;
                    continue;
                }
                const std::optional<Key> key = namedKey(name);
                if (!key) {
                    error = "unknown key <" + name + ">";
                    return std::nullopt;
                }
                keys.push_back(*key);
                i = close + 1;
                continue;
            }
        }

        const auto u = static_cast<unsigned char>(text[i]);
        if (u < 32 || u > 126) {
            error = "unsupported character in key sequence";
            return std::nullopt;
        }
        keys.push_back(Key{KeyType::Char, text[i]});
        ++i;
    }
    return keys;
}

}