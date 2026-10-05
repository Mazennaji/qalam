#include "editor/editor.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

#include "buffer/file_io.h"
#include "text/columns.h"

namespace qalam {
namespace {

constexpr std::size_t kEnd = std::numeric_limits<std::size_t>::max();

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
}

std::size_t countLines(const TextBuffer& buffer, std::size_t bytes) {
    return bytes == 0 ? 0 : buffer.lineCount();
}

std::string describe(const std::string& path, std::size_t lines, std::size_t bytes) {
    return "\"" + path + "\" " + std::to_string(lines) + "L, " + std::to_string(bytes) + "B";
}

}

Editor::Editor(std::unique_ptr<TextBuffer> buffer) : buffer_(std::move(buffer)) {}

void Editor::open(const std::string& path) {
    const LoadResult result = loadFile(path, *buffer_);
    switch (result.status) {
        case LoadStatus::Loaded:
            path_ = path;
            message_ = describe(path, countLines(*buffer_, result.bytes), result.bytes);
            break;
        case LoadStatus::NotFound:
            path_ = path;
            message_ = "\"" + path + "\" [New]";
            break;
        case LoadStatus::Failed:
            message_ = "Cannot read \"" + path + "\"";
            break;
    }
    mode_ = Mode::Normal;
    row_ = 0;
    col_ = 0;
    wantedCol_ = 0;
    rowOff_ = 0;
    colOff_ = 0;
    modified_ = false;
    scroll();
}

void Editor::resize(Size size) {
    size_ = size;
    scroll();
}

void Editor::handleKey(const Key& key) {
    switch (mode_) {
        case Mode::Command:
            handleCommandKey(key);
            break;
        case Mode::Insert:
            handleInsertKey(key);
            break;
        case Mode::Normal:
            message_.clear();
            handleNormalKey(key);
            break;
    }
    scroll();
}

bool Editor::shouldQuit() const {
    return quit_;
}

ViewState Editor::view() const {
    ViewState v;
    v.buffer = buffer_.get();
    switch (mode_) {
        case Mode::Normal: v.mode = "NORMAL"; break;
        case Mode::Insert: v.mode = "INSERT"; break;
        case Mode::Command: v.mode = "COMMAND"; break;
    }
    v.fileName = path_;
    v.modified = modified_;
    v.commandLine = mode_ == Mode::Command;
    v.message = v.commandLine ? ":" + commandText_ : message_;
    v.showWelcome = path_.empty() && buffer_->lineCount() == 1 && buffer_->line(0).empty();
    v.rowOffset = rowOff_;
    v.colOffset = colOff_;
    v.line = row_;
    v.col = col_;
    v.cursorRow = static_cast<int>(row_ - rowOff_);
    v.cursorCol = renderColumn() - colOff_;
    return v;
}

void Editor::handleNormalKey(const Key& key) {
    const bool wasPendingG = pendingG_;
    pendingG_ = false;

    if (pendingFind_ != 0) {
        const char kind = pendingFind_;
        pendingFind_ = 0;
        if (key.type == KeyType::Char) {
            startFind(kind, key.ch);
        } else if (key.type == KeyType::Tab) {
            startFind(kind, '\t');
        }
        return;
    }

    switch (key.type) {
        case KeyType::Up: moveUp(); return;
        case KeyType::Down: moveDown(); return;
        case KeyType::Left: moveLeft(); return;
        case KeyType::Right: moveRight(); return;
        case KeyType::Home: moveToLineStart(); return;
        case KeyType::End: moveToLineEnd(); return;
        case KeyType::PageUp: pageUp(); return;
        case KeyType::PageDown: pageDown(); return;
        case KeyType::Delete: deleteUnderCursor(); return;
        case KeyType::Char: break;
        default: return;
    }

    const Pos here{row_, col_};

    switch (key.ch) {
        case 'h': moveLeft(); break;
        case 'j': moveDown(); break;
        case 'k': moveUp(); break;
        case 'l': moveRight(); break;
        case '0': moveToLineStart(); break;
        case '$': moveToLineEnd(); break;
        case '^': jumpTo(firstNonBlank(*buffer_, here)); break;
        case 'w': jumpTo(wordForward(*buffer_, here, 1)); break;
        case 'b': jumpTo(wordBackward(*buffer_, here, 1)); break;
        case 'e': jumpTo(wordEnd(*buffer_, here, 1)); break;
        case '}': jumpTo(paragraphForward(*buffer_, here, 1)); break;
        case '{': jumpTo(paragraphBackward(*buffer_, here, 1)); break;
        case 'f':
        case 'F':
        case 't':
        case 'T':
            pendingFind_ = key.ch;
            break;
        case ';': repeatFind(false); break;
        case ',': repeatFind(true); break;
        case 'G': moveToLine(buffer_->lineCount() - 1); break;
        case 'g':
            if (wasPendingG) {
                moveToLine(0);
            } else {
                pendingG_ = true;
            }
            break;
        case 'i':
            enterInsert();
            break;
        case 'a':
            col_ = buffer_->line(row_).empty() ? 0 : col_ + 1;
            enterInsert();
            break;
        case 'I':
            col_ = firstNonBlank(*buffer_, {row_, 0}).target.col;
            enterInsert();
            break;
        case 'A':
            col_ = buffer_->line(row_).size();
            enterInsert();
            break;
        case 'o': openLine(row_ + 1); break;
        case 'O': openLine(row_); break;
        case 'x': deleteUnderCursor(); break;
        case ':':
            mode_ = Mode::Command;
            commandText_.clear();
            break;
        default: break;
    }
}

void Editor::handleInsertKey(const Key& key) {
    switch (key.type) {
        case KeyType::Escape: leaveInsert(); break;
        case KeyType::Char: insertText(key.ch); break;
        case KeyType::Tab: insertText('\t'); break;
        case KeyType::Enter: insertNewline(); break;
        case KeyType::Backspace: backspace(); break;
        case KeyType::Delete: deleteForward(); break;
        case KeyType::Up: moveUp(); break;
        case KeyType::Down: moveDown(); break;
        case KeyType::Left: moveLeft(); break;
        case KeyType::Right: moveRight(); break;
        case KeyType::Home: moveToLineStart(); break;
        case KeyType::End: moveToLineEnd(); break;
        case KeyType::PageUp: pageUp(); break;
        case KeyType::PageDown: pageDown(); break;
        default: break;
    }
}

void Editor::handleCommandKey(const Key& key) {
    switch (key.type) {
        case KeyType::Char:
            commandText_ += key.ch;
            break;
        case KeyType::Backspace:
            if (commandText_.empty()) {
                mode_ = Mode::Normal;
            } else {
                commandText_.pop_back();
            }
            break;
        case KeyType::Escape:
            commandText_.clear();
            mode_ = Mode::Normal;
            break;
        case KeyType::Enter: {
            const std::string text = commandText_;
            commandText_.clear();
            mode_ = Mode::Normal;
            runCommand(text);
            break;
        }
        default: break;
    }
}

void Editor::runCommand(const std::string& text) {
    const std::string line = trim(text);
    if (line.empty()) return;

    const auto split = line.find_first_of(" \t");
    std::string name = line.substr(0, split);
    const std::string argument = split == std::string::npos ? "" : trim(line.substr(split));

    bool force = false;
    if (name.size() > 1 && name.back() == '!') {
        force = true;
        name.pop_back();
    }

    if (name == "w" || name == "write") {
        save(argument);
    } else if (name == "q" || name == "quit" || name == "khalas") {
        requestQuit(force);
    } else if (name == "wq" || name == "x") {
        if (save(argument)) requestQuit(true);
    } else {
        message_ = "Not an editor command: " + name;
    }
}

bool Editor::save(const std::string& argument) {
    const std::string target = argument.empty() ? path_ : argument;
    if (target.empty()) {
        message_ = "No file name";
        return false;
    }

    const std::optional<std::size_t> bytes = saveFile(target, *buffer_);
    if (!bytes) {
        message_ = "Cannot write \"" + target + "\"";
        return false;
    }

    if (path_.empty()) path_ = target;
    if (target == path_) modified_ = false;
    message_ = describe(target, countLines(*buffer_, *bytes), *bytes) + " written";
    return true;
}

void Editor::requestQuit(bool force) {
    if (modified_ && !force) {
        message_ = "No write since last change (add ! to override)";
        return;
    }
    quit_ = true;
}

void Editor::enterInsert() {
    mode_ = Mode::Insert;
    wantedCol_ = col_;
}

void Editor::leaveInsert() {
    mode_ = Mode::Normal;
    if (col_ > 0) --col_;
    col_ = std::min(col_, maxColumn(row_));
    wantedCol_ = col_;
}

void Editor::insertText(char c) {
    buffer_->insertChar(row_, col_, c);
    ++col_;
    wantedCol_ = col_;
    modified_ = true;
}

void Editor::insertNewline() {
    buffer_->splitLine(row_, col_);
    ++row_;
    col_ = 0;
    wantedCol_ = 0;
    modified_ = true;
}

void Editor::backspace() {
    if (col_ > 0) {
        buffer_->eraseChar(row_, col_ - 1);
        --col_;
    } else if (row_ > 0) {
        const std::size_t previousLength = buffer_->line(row_ - 1).size();
        buffer_->joinLines(row_ - 1);
        --row_;
        col_ = previousLength;
    } else {
        return;
    }
    wantedCol_ = col_;
    modified_ = true;
}

void Editor::deleteForward() {
    if (col_ < buffer_->line(row_).size()) {
        buffer_->eraseChar(row_, col_);
    } else if (row_ + 1 < buffer_->lineCount()) {
        buffer_->joinLines(row_);
    } else {
        return;
    }
    modified_ = true;
}

void Editor::deleteUnderCursor() {
    if (buffer_->line(row_).empty()) return;
    buffer_->eraseChar(row_, col_);
    col_ = std::min(col_, maxColumn(row_));
    wantedCol_ = col_;
    modified_ = true;
}

void Editor::openLine(std::size_t index) {
    buffer_->insertLine(index, "");
    row_ = index;
    col_ = 0;
    modified_ = true;
    enterInsert();
}

void Editor::moveLeft() {
    if (col_ > 0) --col_;
    wantedCol_ = col_;
}

void Editor::moveRight() {
    if (col_ < maxColumn(row_)) ++col_;
    wantedCol_ = col_;
}

void Editor::moveUp() {
    if (row_ > 0) --row_;
    applyWantedColumn();
}

void Editor::moveDown() {
    if (row_ + 1 < buffer_->lineCount()) ++row_;
    applyWantedColumn();
}

void Editor::moveToLineStart() {
    col_ = 0;
    wantedCol_ = 0;
}

void Editor::moveToLineEnd() {
    col_ = maxColumn(row_);
    wantedCol_ = kEnd;
}

void Editor::moveToLine(std::size_t line) {
    row_ = std::min(line, buffer_->lineCount() - 1);
    applyWantedColumn();
}

void Editor::pageUp() {
    const std::size_t rows = textRows();
    row_ = row_ > rows ? row_ - rows : 0;
    applyWantedColumn();
}

void Editor::pageDown() {
    row_ = std::min(row_ + textRows(), buffer_->lineCount() - 1);
    applyWantedColumn();
}

void Editor::jumpTo(const Motion& motion) {
    row_ = std::min(motion.target.row, buffer_->lineCount() - 1);
    col_ = std::min(motion.target.col, maxColumn(row_));
    wantedCol_ = col_;
}

void Editor::startFind(char kind, char ch) {
    FindState find;
    find.ch = ch;
    find.forward = kind == 'f' || kind == 't';
    find.till = kind == 't' || kind == 'T';
    find.valid = true;
    lastFind_ = find;
    runFind(find, false);
}

void Editor::repeatFind(bool reverse) {
    if (!lastFind_.valid) return;
    FindState find = lastFind_;
    if (reverse) find.forward = !find.forward;
    runFind(find, true);
}

void Editor::runFind(const FindState& find, bool repeat) {
    Pos start{row_, col_};
    if (repeat && find.till) {
        if (find.forward) {
            ++start.col;
        } else if (start.col > 0) {
            --start.col;
        } else {
            return;
        }
    }

    const std::optional<Motion> motion =
        findChar(*buffer_, start, find.ch, find.forward, find.till, 1);
    if (motion) jumpTo(*motion);
}

void Editor::applyWantedColumn() {
    col_ = std::min(wantedCol_, maxColumn(row_));
}

void Editor::scroll() {
    const std::size_t rows = textRows();
    if (row_ < rowOff_) rowOff_ = row_;
    if (row_ >= rowOff_ + rows) rowOff_ = row_ - rows + 1;

    const int width = std::max(1, size_.cols);
    const int column = renderColumn();
    if (column < colOff_) colOff_ = column;
    if (column >= colOff_ + width) colOff_ = column - width + 1;
}

std::size_t Editor::maxColumn(std::size_t row) const {
    const std::size_t length = buffer_->line(row).size();
    if (mode_ == Mode::Insert) return length;
    return length == 0 ? 0 : length - 1;
}

std::size_t Editor::textRows() const {
    return static_cast<std::size_t>(std::max(1, size_.rows - 2));
}

int Editor::renderColumn() const {
    return displayColumn(buffer_->line(row_), col_);
}

}