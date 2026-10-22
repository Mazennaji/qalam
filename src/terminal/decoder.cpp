#include "terminal/decoder.h"

namespace qalam {
    namespace {
        Decoded done(Key key, std::size_t used) {
            Decoded d;
            d.key = std::move(key);
            d.used = used;
            return d;
        }

        Decoded more() {
            Decoded d;
            d.needMore = true;
            return d;
        }

        Key plain(KeyType type) {
            Key key;
            key.type = type;
            return key;
        }

        std::size_t utf8Length(unsigned char lead) {
            if (lead >= 0xC2 && lead <= 0xDF) return 2;
            if (lead >= 0xE0 && lead <= 0xEF) return 3;
            if (lead >= 0xF0 && lead <= 0xF4) return 4;
            return 0;
        }

        int firstParam(const std::string &params) {
            int value = 0;
            for (char c: params) {
                if (c < '0' || c > '9') break;
                value = value * 10 + (c - '0');
                if (value > 1000) break;
            }
            return value;
        }

        KeyType fromLetter(char f) {
            switch (f) {
                case 'A': return KeyType::Up;
                case 'B': return KeyType::Down;
                case 'C': return KeyType::Right;
                case 'D': return KeyType::Left;
                case 'H': return KeyType::Home;
                case 'F': return KeyType::End;
                default: return KeyType::None;
            }
        }

        KeyType fromTilde(int code) {
            switch (code) {
                case 1:
                case 7: return KeyType::Home;
                case 4:
                case 8: return KeyType::End;
                case 3: return KeyType::Delete;
                case 5: return KeyType::PageUp;
                case 6: return KeyType::PageDown;
                default: return KeyType::None;
            }
        }

        Decoded decodeEscape(const std::string &buf, bool final) {
            if (buf.size() == 1) return final ? done(plain(KeyType::Escape), 1) : more();

            const char introducer = buf[1];
            if (introducer != '[' && introducer != 'O') return done(plain(KeyType::Escape), 1);

            std::size_t i = 2;
            std::string params;
            while (i < buf.size() && buf[i] >= 0x30 && buf[i] <= 0x3f) params += buf[i++];
            while (i < buf.size() && buf[i] >= 0x20 && buf[i] <= 0x2f) ++i;

            if (i >= buf.size()) return final ? done(plain(KeyType::Escape), 1) : more();

            const auto last = static_cast<unsigned char>(buf[i]);
            if (last < 0x40 || last > 0x7e) return done(plain(KeyType::None), i);

            const std::size_t used = i + 1;
            KeyType type = KeyType::None;
            if (last == '~') {
                type = fromTilde(firstParam(params));
            } else {
                type = fromLetter(static_cast<char>(last));
            }
            return done(plain(type), used);
        }

        Decoded decodeUtf8(const std::string &buf, bool final) {
            const auto lead = static_cast<unsigned char>(buf[0]);
            const std::size_t length = utf8Length(lead);
            if (length == 0) return done(plain(KeyType::None), 1);

            for (std::size_t i = 1; i < length && i < buf.size(); ++i) {
                if ((static_cast<unsigned char>(buf[i]) & 0xC0) != 0x80) {
                    return done(plain(KeyType::None), i);
                }
            }

            if (buf.size() < length) return final ? done(plain(KeyType::None), buf.size()) : more();

            Key key;
            key.type = KeyType::Text;
            key.text = buf.substr(0, length);
            return done(std::move(key), length);
        }
    }

    Decoded decodeKey(const std::string &bytes, bool final) {
        if (bytes.empty()) return more();

        const auto b = static_cast<unsigned char>(bytes[0]);

        if (b == 0x1b) {
            if (bytes.compare(0, 6, "\x1b[200~") == 0) return decodePaste(bytes, final);
            return decodeEscape(bytes, final);
        }
        if (b == '\r' || b == '\n') return done(plain(KeyType::Enter), 1);
        if (b == '\t') return done(plain(KeyType::Tab), 1);
        if (b == 0x7f || b == 0x08) return done(plain(KeyType::Backspace), 1);

        if (b >= 0x01 && b <= 0x1a) {
            Key key;
            key.type = KeyType::Ctrl;
            key.ch = static_cast<char>('a' + b - 1);
            return done(std::move(key), 1);
        }

        if (b >= 0x20 && b < 0x7f) {
            Key key;
            key.type = KeyType::Char;
            key.ch = static_cast<char>(b);
            return done(std::move(key), 1);
        }

        if (b >= 0x80) return decodeUtf8(bytes, final);

        return done(plain(KeyType::None), 1);
    }

    Decoded decodePaste(const std::string &buf, bool final) {
        static const std::string begin = "\x1b[200~";
        static const std::string end = "\x1b[201~";

        const std::size_t close = buf.find(end, begin.size());
        if (close == std::string::npos) {
            if (!final && buf.size() < (std::size_t{1} << 24)) return more();
            Key key;
            key.type = KeyType::Paste;
            key.text = buf.substr(begin.size());
            return done(std::move(key), buf.size());
        }

        Key key;
        key.type = KeyType::Paste;
        key.text = buf.substr(begin.size(), close - begin.size());
        return done(std::move(key), close + end.size());
    }
}
