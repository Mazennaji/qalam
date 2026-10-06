#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "buffer/text_buffer.h"
#include "motions/motions.h"
#include "render/render.h"
#include "search/search.h"
#include "terminal/terminal.h"
#include "undo/history.h"

namespace qalam {

enum class Mode { Normal, Insert, Command, Search, Visual };

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
    void handleSearchKey(const Key& key);
    void handleVisualKey(const Key& key);
    void runCommand(const std::string& text);
    bool save(const std::string& argument);
    void requestQuit(bool force);

    void enterVisual(char kind);
    void leaveVisual();
    void operateVisual(char op, bool forceLine);
    void visualPut();

    void beginSearch(bool forward);
    void previewSearch();
    void cancelSearch();
    void commitSearch();
    void findNext(bool reverse, int count);
    void substitute(std::size_t first, std::size_t last, std::string pattern,
                    const std::string& replacement, bool global, CaseMode mode);

    void enterInsert();
    void leaveInsert();
    void insertText(char c);
    void insertNewline();
    void backspace();
    void deleteForward();
    void deleteUnderCursor(int count);
    void openLine(bool below);

    void applyEdit(Pos start, Pos end, const std::string& text);
    void undo(int count);
    void redo(int count);
    void placeCursor(Pos where);

    bool hasCount() const;
    int takeCount();
    void resetPending();
    std::string pendingText() const;

    std::optional<Motion> operatorMotion(const Key& key, bool wasPendingG, int count,
                                         bool counted) const;
    void operate(char op, const Motion& motion);
    void put(bool after, int count);

    void moveLeft(std::size_t n = 1);
    void moveRight(std::size_t n = 1);
    void moveUp(std::size_t n = 1);
    void moveDown(std::size_t n = 1);
    void moveToLineStart();
    void moveToLineEnd();
    void moveToLine(std::size_t line);
    void pageUp();
    void pageDown();
    void jumpTo(const Motion& motion);

    void startFind(char kind, char ch);
    void repeatFind(bool reverse, int count);
    void runFind(const FindState& find, bool repeat, int count);

    void applyWantedColumn();
    void scroll();
    std::size_t maxColumn(std::size_t row) const;
    std::size_t textRows() const;
    int renderColumn() const;

    std::unique_ptr<TextBuffer> buffer_;
    UndoHistory history_;
    std::string path_;
    std::string message_;
    std::string commandText_;
    std::string searchText_;
    std::string lastSearch_;
    Mode mode_ = Mode::Normal;
    Size size_{24, 80};
    Pos searchOrigin_;
    Pos anchor_;
    std::size_t row_ = 0;
    std::size_t col_ = 0;
    std::size_t wantedCol_ = 0;
    std::size_t rowOff_ = 0;
    int colOff_ = 0;
    FindState lastFind_;
    Register reg_;
    int count_ = 0;
    int opCount_ = 0;
    char visualKind_ = 'v';
    char pendingFind_ = 0;
    char pendingOp_ = 0;
    bool searchForward_ = true;
    bool lastForward_ = true;
    bool highlight_ = false;
    bool pendingG_ = false;
    bool quit_ = false;
};

}