#include "editor/editor.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <utility>

#include "buffer/file_io.h"
#include "search/substitute.h"
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

struct SubstituteSpec {
    std::size_t first = 0;
    std::size_t last = 0;
    std::string pattern;
    std::string replacement;
    bool global = false;
    CaseMode mode = CaseMode::Smart;
};

enum class Parse { NotSubstitute, Valid, Invalid };

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

std::optional<MapMode> mapModeFor(Mode mode) {
    switch (mode) {
        case Mode::Normal: return MapMode::Normal;
        case Mode::Insert: return MapMode::Insert;
        case Mode::Visual: return MapMode::Visual;
        case Mode::Command:
        case Mode::Search: return std::nullopt;
    }
    return std::nullopt;
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

std::optional<std::size_t> parseAddress(const std::string& s, std::size_t& i,
                                        std::size_t current, std::size_t lineCount) {
    if (i >= s.size()) return std::nullopt;
    if (s[i] == '.') {
        ++i;
        return current;
    }
    if (s[i] == '$') {
        ++i;
        return lineCount - 1;
    }
    if (!std::isdigit(static_cast<unsigned char>(s[i]))) return std::nullopt;

    std::size_t n = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        n = std::min<std::size_t>(n * 10 + static_cast<std::size_t>(s[i] - '0'), 1000000);
        ++i;
    }
    if (n == 0) return std::size_t{0};
    return std::min(n - 1, lineCount - 1);
}

Parse parseSubstitute(const std::string& text, std::size_t current, std::size_t lineCount,
                      SubstituteSpec& spec) {
    std::size_t i = 0;
    spec.first = current;
    spec.last = current;

    if (i < text.size() && text[i] == '%') {
        spec.first = 0;
        spec.last = lineCount - 1;
        ++i;
    } else if (const auto a = parseAddress(text, i, current, lineCount)) {
        spec.first = *a;
        spec.last = *a;
        if (i < text.size() && text[i] == ',') {
            ++i;
            const auto b = parseAddress(text, i, current, lineCount);
            if (!b) return Parse::Invalid;
            spec.last = *b;
        }
    }

    if (i >= text.size() || text[i] != 's') return Parse::NotSubstitute;
    ++i;
    if (i >= text.size()) return Parse::NotSubstitute;

    const char delim = text[i];
    if (std::isalnum(static_cast<unsigned char>(delim)) || delim == ' ' || delim == '\\' ||
        delim == '"') {
        return Parse::NotSubstitute;
    }
    ++i;

    auto readPart = [&](std::string& out) {
        while (i < text.size() && text[i] != delim) {
            if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == delim) {
                out += delim;
                i += 2;
            } else {
                out += text[i++];
            }
        }
        if (i < text.size()) {
            ++i;
            return true;
        }
        return false;
    };

    readPart(spec.pattern);
    const bool closed = readPart(spec.replacement);

    if (closed) {
        for (; i < text.size(); ++i) {
            if (text[i] == 'g') {
                spec.global = true;
            } else if (text[i] == 'i') {
                spec.mode = CaseMode::Insensitive;
            } else if (text[i] == 'I') {
                spec.mode = CaseMode::Sensitive;
            } else {
                return Parse::Invalid;
            }
        }
    }

    if (spec.first > spec.last) std::swap(spec.first, spec.last);
    return Parse::Valid;
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
    highlight_ = false;
    blockInsert_.active = false;
    resetPending();
    history_.clear();
    scroll();
}

bool Editor::useConfig(const std::string& path) {
    configPath_ = path;
    const Config config = loadConfig(path);

    keymap_ = Keymap();
    for (const Mapping& mapping : config.mappings) {
        keymap_.add(mapping.mode, mapping.lhs, mapping.rhs);
    }
    aliases_ = config.aliases;
    timeoutTicks_ = std::max(1, (config.timeoutMs + 99) / 100);
    mapPending_.clear();
    mapTicks_ = 0;

    if (!config.errors.empty()) {
        message_ = path + ": " + config.errors.front();
        if (config.errors.size() > 1) {
            message_ += " (+" + std::to_string(config.errors.size() - 1) + " more)";
        }
    }
    return config.found;
}

void Editor::resize(Size size) {
    size_ = size;
    scroll();
}

void Editor::handleKey(const Key& key) {
    mapPending_.push_back(key);
    mapTicks_ = 0;
    resolveMaps(false);
}

bool Editor::idle() {
    if (mapPending_.empty()) return false;
    if (++mapTicks_ < timeoutTicks_) return false;
    mapTicks_ = 0;
    resolveMaps(true);
    return true;
}

bool Editor::canMap() const {
    switch (mode_) {
        case Mode::Insert:
            return true;
        case Mode::Normal:
            return pendingOp_ == 0 && pendingFind_ == 0 && !pendingG_ && count_ == 0 &&
                   opCount_ == 0;
        case Mode::Visual:
            return pendingFind_ == 0 && !pendingG_ && count_ == 0;
        case Mode::Command:
        case Mode::Search:
            return false;
    }
    return false;
}

void Editor::resolveMaps(bool timedOut) {
    while (!mapPending_.empty()) {
        const std::optional<MapMode> kind = mapModeFor(mode_);
        if (kind && canMap()) {
            const MapLookup found = keymap_.find(*kind, mapPending_);
            if (found.longer && !timedOut) return;
            if (found.exact != nullptr) {
                const std::vector<Key> rhs = *found.exact;
                mapPending_.clear();
                for (const Key& key : rhs) dispatch(key);
                return;
            }
        }

        const Key first = mapPending_.front();
        mapPending_.erase(mapPending_.begin());
        dispatch(first);
    }
}

void Editor::dispatch(const Key& key) {
    switch (mode_) {
        case Mode::Command:
            handleCommandKey(key);
            break;
        case Mode::Search:
            handleSearchKey(key);
            break;
        case Mode::Insert:
            handleInsertKey(key);
            break;
        case Mode::Visual:
            message_.clear();
            handleVisualKey(key);
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
        case Mode::Search: v.mode = "SEARCH"; break;
        case Mode::Visual:
            v.mode = visualKind_ == 'V' ? "V-LINE" : visualKind_ == 'B' ? "V-BLOCK" : "VISUAL";
            break;
    }
    v.fileName = path_;
    v.modified = history_.modified();
    v.commandLine = mode_ == Mode::Command || mode_ == Mode::Search;
    if (mode_ == Mode::Command) {
        v.message = ":" + commandText_;
    } else if (mode_ == Mode::Search) {
        v.message = (searchForward_ ? "/" : "?") + searchText_;
    } else {
        v.message = message_;
    }
    if (mode_ == Mode::Normal || mode_ == Mode::Visual) {
        const std::string pending = pendingText();
        if (!pending.empty()) v.message = pending;
    }
    if (mode_ == Mode::Search) {
        v.searchPattern = searchText_;
    } else if (highlight_) {
        v.searchPattern = lastSearch_;
    }
    if (mode_ == Mode::Visual) {
        Pos a = anchor_;
        Pos b{row_, col_};
        if (b < a) std::swap(a, b);
        Selection selection{a, b, visualKind_ == 'V'};
        if (visualKind_ == 'B') {
            selection.block = true;
            selection.left = std::min(anchor_.col, col_);
            selection.right = std::max(anchor_.col, col_);
        }
        v.selection = selection;
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
            if (key.ch == 'r') {
                redo(n);
            } else if (key.ch == 'v') {
                enterVisual('B');
            }
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
        case '/': beginSearch(true); break;
        case '?': beginSearch(false); break;
        case 'n': findNext(false, n); break;
        case 'N': findNext(true, n); break;
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
        case 'v': enterVisual('v'); break;
        case 'V': enterVisual('V'); break;
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

void Editor::handleVisualKey(const Key& key) {
    if (pendingFind_ != 0) {
        handleNormalKey(key);
        return;
    }

    if (pendingG_) {
        if (key.type == KeyType::Char && key.ch == 'g') {
            handleNormalKey(key);
        } else {
            resetPending();
        }
        return;
    }

    if (key.type == KeyType::Escape) {
        leaveVisual();
        return;
    }

    switch (key.type) {
        case KeyType::Up:
        case KeyType::Down:
        case KeyType::Left:
        case KeyType::Right:
        case KeyType::Home:
        case KeyType::End:
        case KeyType::PageUp:
        case KeyType::PageDown:
            handleNormalKey(key);
            return;
        case KeyType::Ctrl:
            if (key.ch == 'v') {
                if (visualKind_ == 'B') {
                    leaveVisual();
                } else {
                    visualKind_ = 'B';
                }
            }
            return;
        case KeyType::Char: break;
        default: return;
    }

    switch (key.ch) {
        case 'h':
        case 'j':
        case 'k':
        case 'l':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case '$':
        case '^':
        case 'w':
        case 'b':
        case 'e':
        case '{':
        case '}':
        case 'f':
        case 'F':
        case 't':
        case 'T':
        case ';':
        case ',':
        case 'n':
        case 'N':
        case 'G':
        case 'g':
            handleNormalKey(key);
            return;
        case 'v':
        case 'V':
            if (visualKind_ == key.ch) {
                leaveVisual();
            } else {
                visualKind_ = key.ch;
            }
            return;
        case 'o':
        case 'O': {
            const Pos cursor{row_, col_};
            row_ = anchor_.row;
            col_ = anchor_.col;
            anchor_ = cursor;
            wantedCol_ = col_;
            return;
        }
        case 'I':
            if (visualKind_ == 'B') blockInsert(false);
            return;
        case 'A':
            if (visualKind_ == 'B') blockInsert(true);
            return;
        case 'd':
        case 'x': operateVisual('d', false); return;
        case 'D':
        case 'X': operateVisual('d', true); return;
        case 'y': operateVisual('y', false); return;
        case 'Y': operateVisual('y', true); return;
        case 'c':
        case 's': operateVisual('c', false); return;
        case 'C':
        case 'S':
        case 'R': operateVisual('c', true); return;
        case 'p':
        case 'P': visualPut(); return;
        case ':': {
            const std::size_t first = std::min(anchor_.row, row_);
            const std::size_t last = std::max(anchor_.row, row_);
            leaveVisual();
            mode_ = Mode::Command;
            commandText_ = std::to_string(first + 1) + "," + std::to_string(last + 1);
            return;
        }
        default: return;
    }
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

void Editor::handleSearchKey(const Key& key) {
    switch (key.type) {
        case KeyType::Char:
            searchText_ += key.ch;
            previewSearch();
            break;
        case KeyType::Tab:
            searchText_ += '\t';
            previewSearch();
            break;
        case KeyType::Backspace:
            if (searchText_.empty()) {
                cancelSearch();
            } else {
                searchText_.pop_back();
                previewSearch();
            }
            break;
        case KeyType::Escape:
            cancelSearch();
            break;
        case KeyType::Enter:
            commitSearch();
            break;
        default: break;
    }
}

void Editor::runCommand(const std::string& text) {
    const std::string line = trim(text);
    if (line.empty()) return;

    SubstituteSpec spec;
    switch (parseSubstitute(line, row_, buffer_->lineCount(), spec)) {
        case Parse::Valid:
            substitute(spec.first, spec.last, spec.pattern, spec.replacement, spec.global,
                       spec.mode);
            return;
        case Parse::Invalid:
            message_ = "Invalid substitute command";
            return;
        case Parse::NotSubstitute:
            break;
    }

    const auto split = line.find_first_of(" \t");
    std::string name = line.substr(0, split);
    const std::string argument = split == std::string::npos ? "" : trim(line.substr(split));

    bool force = false;
    if (name.size() > 1 && name.back() == '!') {
        force = true;
        name.pop_back();
    }

    if (const auto alias = aliases_.find(name); alias != aliases_.end()) {
        name = alias->second;
    }

    if (name == "w" || name == "write") {
        save(argument);
    } else if (name == "q" || name == "quit" || name == "khalas") {
        requestQuit(force);
    } else if (name == "wq" || name == "x") {
        if (save(argument)) requestQuit(true);
    } else if (name == "noh" || name == "nohlsearch") {
        highlight_ = false;
    } else if (name == "reload") {
        message_.clear();
        const bool found = useConfig(configPath_);
        if (message_.empty()) {
            message_ = found ? "Config reloaded" : "No config file: " + configPath_;
        }
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

void Editor::enterVisual(char kind) {
    resetPending();
    mode_ = Mode::Visual;
    visualKind_ = kind;
    anchor_ = {row_, col_};
}

void Editor::leaveVisual() {
    mode_ = Mode::Normal;
    resetPending();
    col_ = std::min(col_, maxColumn(row_));
}

void Editor::operateVisual(char op, bool forceLine) {
    if (visualKind_ == 'B' && !forceLine) {
        operateBlock(op);
        return;
    }

    takeCount();

    Pos a = anchor_;
    Pos b{row_, col_};
    if (b < a) std::swap(a, b);

    const bool linewise = forceLine || visualKind_ == 'V';
    mode_ = Mode::Normal;
    row_ = a.row;
    col_ = a.col;
    operate(op, Motion{b, linewise, !linewise});
}

void Editor::operateBlock(char op) {
    takeCount();

    const std::size_t top = std::min(anchor_.row, row_);
    const std::size_t bottom = std::max(anchor_.row, row_);
    const std::size_t left = std::min(anchor_.col, col_);
    const std::size_t right = std::max(anchor_.col, col_);

    reg_.rows.clear();
    reg_.text.clear();
    for (std::size_t r = top; r <= bottom; ++r) {
        const std::string& line = buffer_->line(r);
        reg_.rows.push_back(left < line.size() ? line.substr(left, right - left + 1) : "");
        if (r > top) reg_.text += '\n';
        reg_.text += reg_.rows.back();
    }
    reg_.linewise = false;
    reg_.block = true;

    mode_ = Mode::Normal;
    row_ = top;
    col_ = left;

    if (op == 'y') {
        col_ = std::min(col_, maxColumn(row_));
        wantedCol_ = col_;
        return;
    }

    for (std::size_t r = top; r <= bottom; ++r) {
        const std::size_t length = buffer_->line(r).size();
        if (left < length) applyEdit({r, left}, {r, std::min(right + 1, length)}, "");
    }

    if (op == 'c') {
        const std::size_t length = buffer_->line(top).size();
        col_ = std::min(left, length);
        enterInsert();
        blockInsert_ = {true, top, bottom, col_, length};
        return;
    }

    col_ = std::min(left, maxColumn(row_));
    wantedCol_ = col_;
}

void Editor::blockInsert(bool append) {
    takeCount();

    const std::size_t top = std::min(anchor_.row, row_);
    const std::size_t bottom = std::max(anchor_.row, row_);
    const std::size_t left = std::min(anchor_.col, col_);
    const std::size_t right = std::max(anchor_.col, col_);

    mode_ = Mode::Normal;
    row_ = top;
    const std::size_t length = buffer_->line(top).size();
    col_ = std::min(append ? right + 1 : left, length);
    enterInsert();
    blockInsert_ = {true, top, bottom, col_, length};
}

void Editor::finishBlockInsert() {
    const BlockInsert block = blockInsert_;
    blockInsert_.active = false;

    if (row_ != block.top) return;
    const std::string& line = buffer_->line(block.top);
    if (line.size() <= block.oldLen || col_ < block.col) return;

    const std::string typed = line.substr(block.col, line.size() - block.oldLen);
    const std::size_t last = std::min(block.bottom, buffer_->lineCount() - 1);
    for (std::size_t r = block.top + 1; r <= last; ++r) {
        if (buffer_->line(r).size() >= block.col) {
            applyEdit({r, block.col}, {r, block.col}, typed);
        }
    }
}

void Editor::visualPut() {
    const int n = takeCount();

    if (reg_.text.empty()) {
        message_ = "Nothing in register";
        leaveVisual();
        return;
    }

    Pos a = anchor_;
    Pos b{row_, col_};
    if (b < a) std::swap(a, b);

    if (visualKind_ == 'B') {
        const std::size_t left = std::min(anchor_.col, col_);
        const Register previous = reg_;
        operateVisual('d', false);
        const Register deleted = reg_;

        reg_ = previous;
        put(left >= buffer_->line(row_).size(), n);
        reg_ = deleted;
        return;
    }

    const bool linewise = visualKind_ == 'V';
    const bool atEnd = linewise ? b.row + 1 >= buffer_->lineCount()
                                : b.col + 1 >= buffer_->line(b.row).size();

    const Register previous = reg_;
    operateVisual('d', false);
    const Register deleted = reg_;

    reg_ = previous;
    put(atEnd, n);
    reg_ = deleted;
}

void Editor::beginSearch(bool forward) {
    resetPending();
    mode_ = Mode::Search;
    searchForward_ = forward;
    searchText_.clear();
    searchOrigin_ = {row_, col_};
}

void Editor::previewSearch() {
    row_ = searchOrigin_.row;
    col_ = searchOrigin_.col;
    wantedCol_ = col_;
    if (searchText_.empty()) return;

    const std::optional<SearchHit> hit = searchForward_
                                             ? searchForward(*buffer_, searchOrigin_, searchText_)
                                             : searchBackward(*buffer_, searchOrigin_, searchText_);
    if (hit) placeCursor(hit->pos);
}

void Editor::cancelSearch() {
    mode_ = Mode::Normal;
    searchText_.clear();
    row_ = searchOrigin_.row;
    col_ = searchOrigin_.col;
    wantedCol_ = col_;
}

void Editor::commitSearch() {
    mode_ = Mode::Normal;
    const std::string pattern = searchText_;
    searchText_.clear();

    row_ = searchOrigin_.row;
    col_ = searchOrigin_.col;
    wantedCol_ = col_;

    lastForward_ = searchForward_;
    if (!pattern.empty()) lastSearch_ = pattern;
    findNext(false, 1);
}

void Editor::findNext(bool reverse, int count) {
    if (lastSearch_.empty()) {
        message_ = "No previous search pattern";
        return;
    }

    const bool forward = reverse ? !lastForward_ : lastForward_;
    Pos pos{row_, col_};
    bool wrapped = false;

    for (int i = 0; i < count; ++i) {
        const std::optional<SearchHit> hit = forward ? searchForward(*buffer_, pos, lastSearch_)
                                                     : searchBackward(*buffer_, pos, lastSearch_);
        if (!hit) {
            message_ = "Pattern not found: " + lastSearch_;
            return;
        }
        pos = hit->pos;
        wrapped = wrapped || hit->wrapped;
    }

    highlight_ = true;
    placeCursor(pos);

    if (wrapped) {
        message_ = forward ? "search hit BOTTOM, continuing at TOP"
                           : "search hit TOP, continuing at BOTTOM";
    } else {
        message_ = std::string(forward ? "/" : "?") + lastSearch_;
    }
}

void Editor::substitute(std::size_t first, std::size_t last, std::string pattern,
                        const std::string& replacement, bool global, CaseMode mode) {
    if (pattern.empty()) pattern = lastSearch_;
    if (pattern.empty()) {
        message_ = "No previous search pattern";
        return;
    }

    last = std::min(last, buffer_->lineCount() - 1);
    first = std::min(first, last);

    std::size_t total = 0;
    std::size_t lines = 0;
    std::size_t lastRow = first;

    for (std::size_t row = first; row <= last; ++row) {
        const SubstituteResult result =
            substituteLine(buffer_->line(row), pattern, replacement, global, mode);
        if (result.count == 0) continue;
        applyEdit({row, 0}, {row, buffer_->line(row).size()}, result.line);
        total += result.count;
        ++lines;
        lastRow = row;
    }

    lastSearch_ = pattern;
    highlight_ = true;

    if (total == 0) {
        message_ = "Pattern not found: " + pattern;
        return;
    }

    placeCursor({lastRow, firstNonBlank(*buffer_, {lastRow, 0}).target.col});
    message_ = std::to_string(total) + (total == 1 ? " substitution" : " substitutions") +
               " on " + std::to_string(lines) + (lines == 1 ? " line" : " lines");
}

void Editor::enterInsert() {
    mode_ = Mode::Insert;
    wantedCol_ = col_;
}

void Editor::leaveInsert() {
    if (blockInsert_.active) finishBlockInsert();
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

    reg_.block = false;
    reg_.rows.clear();

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

    if (reg_.block) {
        putBlock(after, count);
        return;
    }

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

void Editor::putBlock(bool after, int count) {
    std::size_t width = 0;
    for (const std::string& piece : reg_.rows) width = std::max(width, piece.size());

    const std::size_t length = buffer_->line(row_).size();
    const std::size_t at = after && length > 0 ? std::min(col_ + 1, length) : col_;
    const std::size_t first = row_;

    for (std::size_t i = 0; i < reg_.rows.size(); ++i) {
        const std::string& source = reg_.rows[i];
        const std::size_t gap = width - source.size();

        std::string piece;
        for (int k = 0; k < count; ++k) {
            piece += source;
            piece.append(gap, ' ');
        }

        const std::size_t r = first + i;
        if (r >= buffer_->lineCount()) {
            piece.resize(piece.size() - gap);
            const std::size_t lastRow = buffer_->lineCount() - 1;
            const std::size_t end = buffer_->line(lastRow).size();
            applyEdit({lastRow, end}, {lastRow, end}, "\n" + std::string(at, ' ') + piece);
            continue;
        }

        const std::size_t lineLength = buffer_->line(r).size();
        if (lineLength <= at) piece.resize(piece.size() - gap);

        const std::size_t position = std::min(at, lineLength);
        const std::string padding(at > lineLength ? at - lineLength : 0, ' ');
        applyEdit({r, position}, {r, position}, padding + piece);
    }

    row_ = first;
    col_ = std::min(at, maxColumn(row_));
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