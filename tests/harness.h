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

    Ed &feed(const std::string &keys) {
        std::string spaced;
        for (char c: keys) {
            if (c == ' ') {
                spaced += "<Space>";
            } else {
                spaced += c;
            }
        }

        std::string error;
        const auto parsed = qalam::parseKeys(spaced, error);
        CHECK(parsed.has_value());
        if (parsed) {
            for (const qalam::Key &key: *parsed) e.handleKey(key);
        }
        return *this;
    }

    std::string text() const { return e.bufferText(); }
    std::size_t row() const { return e.cursor().row; }
    std::size_t col() const { return e.cursor().col; }
};
