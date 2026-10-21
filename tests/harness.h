#pragma once

#include <memory>
#include <string>

#include "buffer/line_buffer.h"
#include "check.h"
#include "editor/editor.h"
#include "input/key_notation.h"
#include "text/columns.h"

struct Ed {
    qalam::Editor e;

    explicit Ed(const std::string &text = "") : e(std::make_unique<qalam::LineBuffer>()) {
        qalam::setTabStop(4);
        qalam::Size size;
        size.rows = 24;
        size.cols = 80;
        e.resize(size);
        e.setText(text);
    }

    void sendNotation(const std::string &chunk) {
        if (chunk.empty()) return;
        std::string error;
        const auto parsed = qalam::parseKeys(chunk, error);
        CHECK(parsed.has_value());
        if (parsed) {
            for (const qalam::Key &key: *parsed) e.handleKey(key);
        }
    }

    Ed &feed(const std::string &keys) {
        std::string chunk;
        std::size_t i = 0;
        while (i < keys.size()) {
            const auto byte = static_cast<unsigned char>(keys[i]);
            if (byte >= 128) {
                sendNotation(chunk);
                chunk.clear();
                const std::size_t length = qalam::charLength(keys, i);
                qalam::Key key;
                key.type = qalam::KeyType::Text;
                key.text = keys.substr(i, length);
                e.handleKey(key);
                i += length;
            } else {
                if (keys[i] == ' ') {
                    chunk += "<Space>";
                } else {
                    chunk += keys[i];
                }
                ++i;
            }
        }
        sendNotation(chunk);
        return *this;
    }

    std::string text() const { return e.bufferText(); }
    std::size_t row() const { return e.cursor().row; }
    std::size_t col() const { return e.cursor().col; }
};
