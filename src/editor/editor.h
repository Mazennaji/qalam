#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "buffer/text_buffer.h"
#include "config/config.h"
#include "input/keymap.h"
#include "motions/motions.h"
#include "render/render.h"
#include "search/search.h"
#include "syntax/syntax.h"
#include "terminal/terminal.h"
#include "undo/history.h"

namespace qalam {

enum class Mode { Normal, Insert, Command, Search, Visual };

class Editor {
public:
    explicit Editor(std::unique_ptr<TextBuffer> buffer);

    void open(const std::string& path);
    bool useConfig(const std::string& path);
    void resize(Size size);
    void handleKey(const Key& key);
    bool idle();
    bool shouldQuit() const;
    ViewState view() const;

private:
    struct FindState {
        std::string target;
        bool forward = true;
        bool till = false;
        bool valid = false;
    };

    struct Register {
        std::string text;
        std::vector<std::string> rows;
        bool linewise = false;
        bool block = false;
    };

    struct BlockInsert {
        bool active = false;
        std::size_t top = 0;
        std::size_t bottom = 0;
        std::size_t col = 0;
        std::size_t dcol = 0;
        std::size_t oldLen = 0;
    };

    struct BlockBounds {
        std::size_t top = 0;
        std::size_t bottom = 0;
        std::size_t left = 0;
        std::size_t right = 0;
    };

    void dispatch(const Key& key);
    void resolveMaps(bool timedOut);
    bool canMap() const;

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
    void operateBlock(char op);
    void blockInsert(bool append);
    void finishBlockInsert();
    void visualPut();
    BlockBounds blockBounds() const;

    void beginSearch(bool forward);
    void previewSearch();
    void cancelSearch();
    void commitSearch();
    void findNext(bool reverse, int count);
    void substitute(std::size_t first, std::size_t last, std::string pattern,
                    const std::string& replacement, bool global, CaseMode mode);

    void enterInsert();
    void leaveInsert();
    void insertString(const std::string& text);
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
    void putBlock(bool after, int count);

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

    void startFind(char kind, const std::string& target);
    void repeatFind(bool reverse, int count);
    void runFind(const FindState& find, bool repeat, int count);

    void rememberColumn();
    void applyWantedColumn();
    void scroll();
    std::size_t maxColumn(std::size_t row) const;
    std::size_t textRows() const;
    int renderColumn() const;

    std::unique_ptr<TextBuffer> buffer_;
    UndoHistory history_;
    Keymap keymap_;
    Syntax syntax_;
    std::map<std::string, std::string> aliases_;
    std::vector<Key> mapPending_;
    std::string configPath_;
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
    BlockInsert blockInsert_;
    int count_ = 0;
    int opCount_ = 0;
    int mapTicks_ = 0;
    int timeoutTicks_ = 5;
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