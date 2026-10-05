#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "buffer/text_buffer.h"
#include "render/render.h"
#include "terminal/terminal.h"

namespace qalam {

    enum class Mode { Normal, Insert, Command };

    class Editor {
    public:
        explicit Editor(std::unique_ptr<TextBuffer> buffer);

        void open(const std::string& path);
        void resize(Size size);
        void handleKey(const Key& key);
        bool shouldQuit() const;
        ViewState view() const;

    private:
        void handleNormalKey(const Key& key);
        void handleInsertKey(const Key& key);
        void handleCommandKey(const Key& key);
        void runCommand(const std::string& text);
        bool save(const std::string& argument);
        void requestQuit(bool force);

        void enterInsert();
        void leaveInsert();
        void insertText(char c);
        void insertNewline();
        void backspace();
        void deleteForward();
        void deleteUnderCursor();
        void openLine(std::size_t index);

        void moveLeft();
        void moveRight();
        void moveUp();
        void moveDown();
        void moveToLineStart();
        void moveToLineEnd();
        void moveToLine(std::size_t line);
        void pageUp();
        void pageDown();

        void applyWantedColumn();
        void scroll();
        std::size_t maxColumn(std::size_t row) const;
        std::size_t firstNonBlank(std::size_t row) const;
        std::size_t textRows() const;
        int renderColumn() const;

        std::unique_ptr<TextBuffer> buffer_;
        std::string path_;
        std::string message_;
        std::string commandText_;
        Mode mode_ = Mode::Normal;
        Size size_{24, 80};
        std::size_t row_ = 0;
        std::size_t col_ = 0;
        std::size_t wantedCol_ = 0;
        std::size_t rowOff_ = 0;
        int colOff_ = 0;
        bool modified_ = false;
        bool pendingG_ = false;
        bool quit_ = false;
    };

}