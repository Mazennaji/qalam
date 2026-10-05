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
constexpr int kMaxCount = 99999;

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
    resetPending();
    history_.clear();
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
    if (mode_ != Mode::Insert) history_.close();
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
    v.modified = history_.modified();
    v.commandLine = mode_ == Mode::Command;
    v.message = v.commandLine ? ":" + commandText_ : message_;
    if (mode_ == Mode::Normal) {
        const std::string pending = pendingText();
        if (!pending.empty()) v.message = pending;
    }
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
            resetPending();
        }
        return;
    }

    if (key.type == KeyType::Escape) {
        resetPending();
        return;
    }

    if (key.type == KeyType::Char &&
        ((key.ch >= '1' && key.ch <= '9') || (key.ch == '0' && count_ > 0))) {
        count_ = std::min(count_ * 10 + (key.ch - '0'), kMaxCount);
        return;
    }

    if (pendingOp_ != 0) {
        handleOperatorKey(key, wasPendingG);
        return;
    }

    if (key.type == KeyType::Char) {
        switch (key.ch) {
            case 'f':
            case 'F':
            case 't':
            case 'T':
                pendingFind_ = key.ch;
                return;
            case 'd':
            case 'c':
            case 'y':
                pendingOp_ = key.ch;
                opCount_ = count_;
                count_ = 0;
                return;
            case 'g':
                if (!wasPendingG) {
                    pendingG_ = true;
                    return;
                }
                break;
            default: break;
        }
    }

    const bool counted = hasCount();
    const int n = takeCount();
    const auto un = static_cast<std::size_t>(n);
    const Pos here{row_, col_};

    switch (key.type) {
        case KeyType::Up: moveUp(un); return;
        case KeyType::Down: moveDown(un); return;
        case KeyType::Left: moveLeft(un); return;
        case KeyType::Right: moveRight(un); return;
        case KeyType::Home: moveToLineStart(); return;
        case KeyType::End: moveToLineEnd(); return;
        case KeyType::PageUp: pageUp(); return;
        case KeyType::PageDown: pageDown(); return;
        case KeyType::Delete: deleteUnderCursor(n); return;
        case KeyType::Ctrl:
            if (key.ch == 'r') redo(n);
            return;
        case KeyType::Char: break;
        default: return;
    }

    switch (key.ch) {
        case 'h': moveLeft(un); break;
        case 'j': moveDown(un); break;
        case 'k': moveUp(un); break;
        case 'l': moveRight(un); break;
        case '0': moveToLineStart(); break;
        case '$': moveToLineEnd(); break;
        case '^': jumpTo(firstNonBlank(*buffer_, here)); break;
        case 'w': jumpTo(wordForward(*buffer_, here, n)); break;
        case 'b': jumpTo(wordBackward(*buffer_, here, n)); break;
        case 'e': jumpTo(wordEnd(*buffer_, here, n)); break;
        case '}': jumpTo(paragraphForward(*buffer_, here, n)); break;
        case '{': jumpTo(paragraphBackward(*buffer_, here, n)); break;
        case ';': repeatFind(false, n); break;
        case ',': repeatFind(true, n); break;
        case 'G': moveToLine(counted ? un - 1 : buffer_->lineCount() - 1); break;
        case 'g': moveToLine(counted ? un - 1 : 0); break;
        case 'D': operate('d', lineEnd(*buffer_, here)); break;
        case 'C': operate('c', lineEnd(*buffer_, here)); break;
        case 'Y': operate('y', gotoLine(*buffer_, row_ + un - 1)); break;
        case 'S': operate('c', gotoLine(*buffer_, row_ + un - 1)); break;
        case 's':
            if (const auto m = charRight(*buffer_, here, n)) {
                operate('c', *m);
            } else {
                enterInsert();
            }
            break;
        case 'X':
            if (const auto m = charLeft(here, n)) operate('d', *m);
            break;
        case 'x': deleteUnderCursor(n); break;
        case 'p': put(true, n); break;
        case 'P': put(false, n); break;
        case 'u': undo(n); break;
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
        case 'o': openLine(true); break;
        case 'O': openLine(false); break;
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
            const auto n = static_cast<std::size_t>(takeCount());
            operate(op, gotoLine(*buffer_, row_ + n - 1));
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
            case ';':
            case ',':
                repeatFind(key.ch == ',', takeCount());
                return;
            default: break;
        }
    }

    const bool counted = hasCount();
    const int n = takeCount();
    const std::optional<Motion> motion = operatorMotion(key, wasPendingG, n, counted);
    if (!motion) {
        resetPending();
        return;
    }
    operate(op, *motion);
}

void Editor::handleInsertKey(const Key& key) {
    switch (key.type) {
        case KeyType::Up:
        case KeyType::Down:
        case KeyType::Left:
        case KeyType::Right:
        case KeyType::Home:
        case KeyType::End:
        case KeyType::PageUp:
        case KeyType::PageDown:
            history_.close();
            break;
        default: break;
    }

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
    if (target == path_) history_.markSaved();
    message_ = describe(target, countLines(*buffer_, *bytes), *bytes) + " written";
    return true;
}

void Editor::requestQuit(bool force) {
    if (history_.modified() && !force) {
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
    applyEdit({row_, col_}, {row_, col_}, std::string(1, c));
    ++col_;
    wantedCol_ = col_;
}

void Editor::insertNewline() {
    applyEdit({row_, col_}, {row_, col_}, "\n");
    ++row_;
    col_ = 0;
    wantedCol_ = 0;
}

void Editor::backspace() {
    if (col_ > 0) {
        applyEdit({row_, col_ - 1}, {row_, col_}, "");
        --col_;
    } else if (row_ > 0) {
        const std::size_t previousLength = buffer_->line(row_ - 1).size();
        applyEdit({row_ - 1, previousLength}, {row_, 0}, "");
        --row_;
        col_ = previousLength;
    } else {
        return;
    }
    wantedCol_ = col_;
}

void Editor::deleteForward() {
    const std::size_t length = buffer_->line(row_).size();
    if (col_ < length) {
        applyEdit({row_, col_}, {row_, col_ + 1}, "");
    } else if (row_ + 1 < buffer_->lineCount()) {
        applyEdit({row_, length}, {row_ + 1, 0}, "");
    }
}

void Editor::deleteUnderCursor(int count) {
    if (const auto motion = charRight(*buffer_, {row_, col_}, count)) operate('d', *motion);
}

void Editor::openLine(bool below) {
    if (below) {
        const std::size_t length = buffer_->line(row_).size();
        applyEdit({row_, length}, {row_, length}, "\n");
        ++row_;
    } else {
        applyEdit({row_, 0}, {row_, 0}, "\n");
    }
    col_ = 0;
    enterInsert();
}

void Editor::applyEdit(Pos start, Pos end, const std::string& text) {
    history_.record(buffer_->replace(start, end, text));
}

void Editor::undo(int count) {
    std::optional<Pos> where;
    for (int i = 0; i < count; ++i) {
        const std::optional<Pos> next = history_.undo(*buffer_);
        if (!next) break;
        where = next;
    }
    if (where) {
        placeCursor(*where);
    } else {
        message_ = "Already at oldest change";
    }
}

void Editor::redo(int count) {
    std::optional<Pos> where;
    for (int i = 0; i < count; ++i) {
        const std::optional<Pos> next = history_.redo(*buffer_);
        if (!next) break;
        where = next;
    }
    if (where) {
        placeCursor(*where);
    } else {
        message_ = "Already at newest change";
    }
}

void Editor::placeCursor(Pos where) {
    row_ = std::min(where.row, buffer_->lineCount() - 1);
    col_ = std::min(where.col, maxColumn(row_));
    wantedCol_ = col_;
}

bool Editor::hasCount() const {
    return count_ > 0 || opCount_ > 0;
}

int Editor::takeCount() {
    const long long outer = opCount_ > 0 ? opCount_ : 1;
    const long long inner = count_ > 0 ? count_ : 1;
    count_ = 0;
    opCount_ = 0;
    return static_cast<int>(std::min<long long>(outer * inner, kMaxCount));
}

void Editor::resetPending() {
    pendingOp_ = 0;
    pendingFind_ = 0;
    pendingG_ = false;
    count_ = 0;
    opCount_ = 0;
}

std::string Editor::pendingText() const {
    std::string text;
    if (opCount_ > 0) text += std::to_string(opCount_);
    if (pendingOp_ != 0) text += pendingOp_;
    if (count_ > 0) text += std::to_string(count_);
    if (pendingFind_ != 0) text += pendingFind_;
    if (pendingG_) text += 'g';
    return text;
}

std::optional<Motion> Editor::operatorMotion(const Key& key, bool wasPendingG, int count,
                                             bool counted) const {
    const Pos here{row_, col_};

    switch (key.type) {
        case KeyType::Left: return charLeft(here, count);
        case KeyType::Right: return charRight(*buffer_, here, count);
        case KeyType::Up: return lineUp(here, count);
        case KeyType::Down: return lineDown(*buffer_, here, count);
        case KeyType::Home: return lineStart(here);
        case KeyType::End: return lineEnd(*buffer_, here);
        case KeyType::Char: break;
        default: return std::nullopt;
    }

    const auto line = static_cast<std::size_t>(count) - 1;

    switch (key.ch) {
        case 'h': return charLeft(here, count);
        case 'l': return charRight(*buffer_, here, count);
        case 'k': return lineUp(here, count);
        case 'j': return lineDown(*buffer_, here, count);
        case '0': return lineStart(here);
        case '$': return lineEnd(*buffer_, here);
        case '^': return firstNonBlank(*buffer_, here);
        case 'w':
            if (pendingOp_ == 'c') return changeWord(*buffer_, here, count);
            return wordForwardForOperator(*buffer_, here, count);
        case 'b': return wordBackward(*buffer_, here, count);
        case 'e': return wordEnd(*buffer_, here, count);
        case '}': return paragraphForward(*buffer_, here, count);
        case '{': return paragraphBackward(*buffer_, here, count);
        case 'G': return gotoLine(*buffer_, counted ? line : buffer_->lineCount() - 1);
        case 'g':
            if (wasPendingG) return gotoLine(*buffer_, counted ? line : 0);
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

    if (range.linewise && op == 'c') {
        const std::size_t last = range.end.row;
        applyEdit({range.start.row, 0}, {last, buffer_->line(last).size()}, "");
        row_ = range.start.row;
        col_ = 0;
        enterInsert();
        return;
    }

    if (range.linewise) {
        const std::size_t first = range.start.row;
        const std::size_t last = range.end.row;
        if (last + 1 < buffer_->lineCount()) {
            applyEdit({first, 0}, {last + 1, 0}, "");
        } else if (first > 0) {
            applyEdit({first - 1, buffer_->line(first - 1).size()},
                      {last, buffer_->line(last).size()}, "");
        } else {
            applyEdit({0, 0}, {last, buffer_->line(last).size()}, "");
        }
        row_ = std::min(first, buffer_->lineCount() - 1);
        col_ = firstNonBlank(*buffer_, {row_, 0}).target.col;
        col_ = std::min(col_, maxColumn(row_));
        wantedCol_ = col_;
        return;
    }

    applyEdit(range.start, range.end, "");
    row_ = range.start.row;
    col_ = range.start.col;
    if (op == 'c') {
        enterInsert();
        return;
    }
    col_ = std::min(col_, maxColumn(row_));
    wantedCol_ = col_;
}

void Editor::put(bool after, int count) {
    if (reg_.text.empty()) return;

    if (reg_.linewise) {
        std::string body = reg_.text;
        if (!body.empty() && body.back() == '\n') body.pop_back();

        std::string block = body;
        for (int i = 1; i < count; ++i) block += "\n" + body;

        if (!after) {
            applyEdit({row_, 0}, {row_, 0}, block + "\n");
        } else if (row_ + 1 < buffer_->lineCount()) {
            applyEdit({row_ + 1, 0}, {row_ + 1, 0}, block + "\n");
            ++row_;
        } else {
            const std::size_t length = buffer_->line(row_).size();
            applyEdit({row_, length}, {row_, length}, "\n" + block);
            ++row_;
        }
        col_ = firstNonBlank(*buffer_, {row_, 0}).target.col;
    } else {
        std::string payload;
        for (int i = 0; i < count; ++i) payload += reg_.text;

        const std::size_t length = buffer_->line(row_).size();
        const std::size_t at = after && length > 0 ? std::min(col_ + 1, length) : col_;
        applyEdit({row_, at}, {row_, at}, payload);
        if (payload.find('\n') == std::string::npos) {
            col_ = at + payload.size() - 1;
        } else {
            col_ = at;
        }
    }

    col_ = std::min(col_, maxColumn(row_));
    wantedCol_ = col_;
}

void Editor::moveLeft(std::size_t n) {
    col_ = col_ > n ? col_ - n : 0;
    wantedCol_ = col_;
}

void Editor::moveRight(std::size_t n) {
    col_ = std::min(col_ + n, maxColumn(row_));
    wantedCol_ = col_;
}

void Editor::moveUp(std::size_t n) {
    row_ = row_ > n ? row_ - n : 0;
    applyWantedColumn();
}

void Editor::moveDown(std::size_t n) {
    row_ = std::min(row_ + n, buffer_->lineCount() - 1);
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
    runFind(find, false, takeCount());
}

void Editor::repeatFind(bool reverse, int count) {
    if (!lastFind_.valid) {
        resetPending();
        return;
    }
    FindState find = lastFind_;
    if (reverse) find.forward = !find.forward;
    runFind(find, true, count);
}

void Editor::runFind(const FindState& find, bool repeat, int count) {
    Pos start{row_, col_};
    if (repeat && find.till) {
        if (find.forward) {
            ++start.col;
        } else if (start.col > 0) {
            --start.col;
        } else {
            resetPending();
            return;
        }
    }

    const std::optional<Motion> motion =
        findChar(*buffer_, start, find.ch, find.forward, find.till, count);

    if (motion && pendingOp_ != 0) {
        operate(pendingOp_, *motion);
        return;
    }
    resetPending();
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