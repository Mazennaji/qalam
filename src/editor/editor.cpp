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

struct Range {
    Pos start;
    Pos end;
    bool linewise;
};

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

Range makeRange(const TextBuffer& buffer, Pos from, const Motion& motion) {
    Pos a = from;
    Pos b = motion.target;
    if (b < a) std::swap(a, b);
    if (motion.linewise) return {a, b, true};

    if (motion.inclusive) {
        b.col = std::min(b.col + 1, buffer.line(b.row).size());
        return {a, b, false};
    }

    if (b.col == 0 && b.row > a.row) {
        --b.row;
        if (a.col <= firstNonBlank(buffer, a).target.col) return {a, b, true};
        b.col = buffer.line(b.row).size();
    }
    return {a, b, false};
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
    pendingOp_ = 0;
    pendingFind_ = 0;
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
    if (mode_ == Mode::Normal && pendingOp_ != 0) v.message = std::string(1, pendingOp_);
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
        } else {
            pendingOp_ = 0;
        }
        return;
    }

    if (key.type == KeyType::Escape) {
        pendingOp_ = 0;
        return;
    }

    if (pendingOp_ != 0) {
        handleOperatorKey(key, wasPendingG);
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
        case 'd':
        case 'c':
        case 'y':
            pendingOp_ = key.ch;
            break;
        case 'D': operate('d', lineEnd(*buffer_, here)); break;
        case 'C': operate('c', lineEnd(*buffer_, here)); break;
        case 'Y': operate('y', gotoLine(*buffer_, row_)); break;
        case 'S': operate('c', gotoLine(*buffer_, row_)); break;
        case 's':
            if (const auto m = charRight(*buffer_, here, 1)) {
                operate('c', *m);
            } else {
                enterInsert();
            }
            break;
        case 'X':
            if (const auto m = charLeft(here, 1)) operate('d', *m);
            break;
        case 'x': deleteUnderCursor(); break;
        case 'p': put(true); break;
        case 'P': put(false); break;
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
        case ':':
            mode_ = Mode::Command;
            commandText_.clear();
            break;
        default: break;
    }
}

void Editor::handleOperatorKey(const Key& key, bool wasPendingG) {
    const char op = pendingOp_;

    if (key.type == KeyType::Char) {
        if (!wasPendingG && key.ch == op) {
            operate(op, gotoLine(*buffer_, row_));
            return;
        }
        switch (key.ch) {
            case 'g':
                if (!wasPendingG) {
                    pendingG_ = true;
                    return;
                }
                break;
            case 'f':
            case 'F':
            case 't':
            case 'T':
                pendingFind_ = key.ch;
                return;
            default: break;
        }
    }

    const std::optional<Motion> motion = operatorMotion(key, wasPendingG);
    if (!motion) {
        pendingOp_ = 0;
        return;
    }
    operate(op, *motion);
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
    if (const auto motion = charRight(*buffer_, {row_, col_}, 1)) operate('d', *motion);
}

void Editor::openLine(std::size_t index) {
    buffer_->insertLine(index, "");
    row_ = index;
    col_ = 0;
    modified_ = true;
    enterInsert();
}

std::optional<Motion> Editor::operatorMotion(const Key& key, bool wasPendingG) const {
    const Pos here{row_, col_};

    switch (key.type) {
        case KeyType::Left: return charLeft(here, 1);
        case KeyType::Right: return charRight(*buffer_, here, 1);
        case KeyType::Up: return lineUp(here, 1);
        case KeyType::Down: return lineDown(*buffer_, here, 1);
        case KeyType::Home: return lineStart(here);
        case KeyType::End: return lineEnd(*buffer_, here);
        case KeyType::Char: break;
        default: return std::nullopt;
    }

    switch (key.ch) {
        case 'h': return charLeft(here, 1);
        case 'l': return charRight(*buffer_, here, 1);
        case 'k': return lineUp(here, 1);
        case 'j': return lineDown(*buffer_, here, 1);
        case '0': return lineStart(here);
        case '$': return lineEnd(*buffer_, here);
        case '^': return firstNonBlank(*buffer_, here);
        case 'w':
            if (pendingOp_ == 'c') return changeWord(*buffer_, here, 1);
            return wordForwardForOperator(*buffer_, here, 1);
        case 'b': return wordBackward(*buffer_, here, 1);
        case 'e': return wordEnd(*buffer_, here, 1);
        case '}': return paragraphForward(*buffer_, here, 1);
        case '{': return paragraphBackward(*buffer_, here, 1);
        case 'G': return gotoLine(*buffer_, buffer_->lineCount() - 1);
        case 'g':
            if (wasPendingG) return gotoLine(*buffer_, 0);
            return std::nullopt;
        default: return std::nullopt;
    }
}

void Editor::operate(char op, const Motion& motion) {
    pendingOp_ = 0;
    const Range range = makeRange(*buffer_, {row_, col_}, motion);

    if (!range.linewise && range.start == range.end) {
        if (op == 'c') enterInsert();
        return;
    }

    if (range.linewise) {
        const std::size_t last = range.end.row;
        reg_.text =
            buffer_->text({range.start.row, 0}, {last, buffer_->line(last).size()}) + "\n";
        reg_.linewise = true;
    } else {
        reg_.text = buffer_->text(range.start, range.end);
        reg_.linewise = false;
    }

    if (op == 'y') {
        row_ = range.start.row;
        if (!range.linewise) col_ = range.start.col;
        col_ = std::min(col_, maxColumn(row_));
        wantedCol_ = col_;
        return;
    }

    modified_ = true;

    if (range.linewise && op == 'c') {
        const std::size_t last = range.end.row;
        buffer_->replace({range.start.row, 0}, {last, buffer_->line(last).size()}, "");
        row_ = range.start.row;
        col_ = 0;
        enterInsert();
        return;
    }

    if (range.linewise) {
        const std::size_t first = range.start.row;
        const std::size_t last = range.end.row;
        if (last + 1 < buffer_->lineCount()) {
            buffer_->replace({first, 0}, {last + 1, 0}, "");
        } else if (first > 0) {
            buffer_->replace({first - 1, buffer_->line(first - 1).size()},
                             {last, buffer_->line(last).size()}, "");
        } else {
            buffer_->replace({0, 0}, {last, buffer_->line(last).size()}, "");
        }
        row_ = std::min(first, buffer_->lineCount() - 1);
        col_ = firstNonBlank(*buffer_, {row_, 0}).target.col;
        col_ = std::min(col_, maxColumn(row_));
        wantedCol_ = col_;
        return;
    }

    buffer_->replace(range.start, range.end, "");
    row_ = range.start.row;
    col_ = range.start.col;
    if (op == 'c') {
        enterInsert();
        return;
    }
    col_ = std::min(col_, maxColumn(row_));
    wantedCol_ = col_;
}

void Editor::put(bool after) {
    if (reg_.text.empty()) return;

    if (reg_.linewise) {
        std::string body = reg_.text;
        if (!body.empty() && body.back() == '\n') body.pop_back();

        if (!after) {
            buffer_->replace({row_, 0}, {row_, 0}, body + "\n");
        } else if (row_ + 1 < buffer_->lineCount()) {
            buffer_->replace({row_ + 1, 0}, {row_ + 1, 0}, body + "\n");
            ++row_;
        } else {
            const std::size_t length = buffer_->line(row_).size();
            buffer_->replace({row_, length}, {row_, length}, "\n" + body);
            ++row_;
        }
        col_ = firstNonBlank(*buffer_, {row_, 0}).target.col;
    } else {
        const std::size_t length = buffer_->line(row_).size();
        const std::size_t at = after && length > 0 ? std::min(col_ + 1, length) : col_;
        buffer_->replace({row_, at}, {row_, at}, reg_.text);
        if (reg_.text.find('\n') == std::string::npos) {
            col_ = at + reg_.text.size() - 1;
        } else {
            col_ = at;
        }
    }

    col_ = std::min(col_, maxColumn(row_));
    wantedCol_ = col_;
    modified_ = true;
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
            pendingOp_ = 0;
            return;
        }
    }

    const std::optional<Motion> motion =
        findChar(*buffer_, start, find.ch, find.forward, find.till, 1);

    if (motion && pendingOp_ != 0) {
        operate(pendingOp_, *motion);
        return;
    }
    pendingOp_ = 0;
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