#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "buffer/text_buffer.h"
#include "motions/motions.h"
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
    struct FindState {
        char ch = 0;
        bool forward = true;
        bool till = false;
        bool valid = false;
    };

    struct Register {
        std::string text;
        bool linewise = false;
    };

    void handleNormalKey(const Key& key);
    void handleOperatorKey(const Key& key, bool wasPendingG);
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

    std::optional<Motion> operatorMotion(const Key& key, bool wasPendingG) const;
    void operate(char op, const Motion& motion);
    void put(bool after);

    void moveLeft();
    void moveRight();
    void moveUp();
    void moveDown();
    void moveToLineStart();
    void moveToLineEnd();
    void moveToLine(std::size_t line);
    void pageUp();
    void pageDown();
    void jumpTo(const Motion& motion);

    void startFind(char kind, char ch);
    void repeatFind(bool reverse);
    void runFind(const FindState& find, bool repeat);

    void applyWantedColumn();
    void scroll();
    std::size_t maxColumn(std::size_t row) const;
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
    FindState lastFind_;
    Register reg_;
    char pendingFind_ = 0;
    char pendingOp_ = 0;
    bool modified_ = false;
    bool pendingG_ = false;
    bool quit_ = false;
};

}