#include "editor/editor.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <utility>

#include "buffer/file_io.h"
#include "buffer/line_buffer.h"
#include "search/substitute.h"
#include "text/columns.h"

namespace qalam {
namespace {

constexpr std::size_t kEnd = std::numeric_limits<std::size_t>::max();
constexpr int kMaxCount = 99999;

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

TextRange makeRange(const TextBuffer& buffer, Pos from, const Motion& motion) {
    Pos a = from;
    Pos b = motion.target;
    if (b < a) std::swap(a, b);
    if (motion.linewise) return {a, b, true};

    if (motion.inclusive) {
        const std::string& line = buffer.line(b.row);
        b.col = std::min(b.col + charLength(line, b.col), line.size());
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

Editor::Editor(std::unique_ptr<TextBuffer> buffer) : buffer_(std::move(buffer)) {
    docs_.emplace_back();
    windows_.emplace_back();
}

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
    syntax_.setFile(path_);
    mode_ = Mode::Normal;
    row_ = 0;
    col_ = 0;
    wantedCol_ = 0;
    rowOff_ = 0;
    colOff_ = 0;
    highlight_ = false;
    blockInsert_.active = false;
    lastChange_.clear();
    cmdKeys_.clear();
    changed_ = false;
    tainted_ = false;
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
    syntax_.resetColors();
    for (const auto& [name, code] : config.colors) syntax_.setColor(name, code);
    syntax_.setEnabled(config.syntax);
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
            return pendingOp_ == 0 && pendingFind_ == 0 && !pendingG_ && !pendingReplace_ &&
                   !pendingWindow_ && pendingObject_ == 0 && count_ == 0 && opCount_ == 0;
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
    if (!replaying_) cmdKeys_.push_back(key);

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
    if (!replaying_) trackRepeat();
    scroll();
}

void Editor::trackRepeat() {
    if (mode_ == Mode::Visual || mode_ == Mode::Command || mode_ == Mode::Search) {
        tainted_ = true;
        cmdKeys_.clear();
        return;
    }
    if (mode_ == Mode::Insert) return;
    if (pendingOp_ != 0 || pendingFind_ != 0 || pendingG_ || pendingReplace_ ||
        pendingObject_ != 0 || count_ != 0 || opCount_ != 0) {
        return;
    }

    if (changed_ && !tainted_) lastChange_ = cmdKeys_;
    cmdKeys_.clear();
    changed_ = false;
    tainted_ = false;
}

void Editor::repeatChange(int count) {
    if (lastChange_.empty()) return;

    const std::vector<Key> keys = lastChange_;
    replaying_ = true;
    for (int i = 0; i < count; ++i) {
        for (const Key& key : keys) dispatch(key);
    }
    replaying_ = false;

    cmdKeys_.clear();
    changed_ = false;
    tainted_ = false;
}

bool Editor::shouldQuit() const {
    return quit_;
}

ViewState Editor::view() const {
    ViewState v;

    std::string modeName;
    switch (mode_) {
        case Mode::Normal: modeName = "NORMAL"; break;
        case Mode::Insert: modeName = "INSERT"; break;
        case Mode::Command: modeName = "COMMAND"; break;
        case Mode::Search: modeName = "SEARCH"; break;
        case Mode::Visual:
            modeName = visualKind_ == 'V' ? "V-LINE" : visualKind_ == 'B' ? "V-BLOCK" : "VISUAL";
            break;
    }

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

    auto title = [&](std::size_t doc) {
        std::string name = docPath(doc);
        if (docs_.size() > 1) {
            if (name.empty()) name = "[No Name]";
            name += " (" + std::to_string(doc + 1) + "/" + std::to_string(docs_.size()) + ")";
        }
        return name;
    };

    std::vector<Rect> separators;
    const std::vector<Placement> placed = layout_.place(screenArea(), &separators);
    v.separators = std::move(separators);

    for (const Placement& p : placed) {
        const std::size_t index = indexOfWindow(p.id);
        WindowView w;
        w.rect = p.rect;
        w.active = index == activeWin_;

        if (w.active) {
            w.buffer = buffer_.get();
            w.syntax = &syntax_;
            w.mode = modeName;
            w.fileName = title(current_);
            w.modified = history_.modified();
            if (mode_ == Mode::Visual) {
                Pos a = anchor_;
                Pos b{row_, col_};
                if (b < a) std::swap(a, b);
                Selection selection{a, b, visualKind_ == 'V'};
                if (visualKind_ == 'B') {
                    const BlockBounds bounds = blockBounds();
                    selection.block = true;
                    selection.left = bounds.left;
                    selection.right = bounds.right;
                }
                w.selection = selection;
            }
            w.showWelcome =
                path_.empty() && buffer_->lineCount() == 1 && buffer_->line(0).empty();
            w.rowOffset = rowOff_;
            w.colOffset = colOff_;
            w.line = row_;
            w.col = static_cast<std::size_t>(renderColumn());
            w.cursorRow = static_cast<int>(row_ - rowOff_);
            w.cursorCol = renderColumn() - colOff_;
        } else {
            const Window& win = windows_[index];
            const std::size_t doc = std::min(win.doc, docs_.size() - 1);
            w.buffer = doc == current_ ? buffer_.get() : docs_[doc].buffer.get();
            w.syntax = doc == current_ ? &syntax_ : nullptr;
            w.fileName = title(doc);
            w.modified = docModified(doc);

            const TextBuffer& text = *w.buffer;
            const std::size_t r = std::min(win.row, text.lineCount() - 1);
            const std::string& line = text.line(r);
            const std::size_t limit = line.empty() ? 0 : prevCharStart(line, line.size());
            const std::size_t c = snapToStart(line, std::min(win.col, limit));
            const int column = displayColumn(line, c);

            const auto rows = static_cast<std::size_t>(std::max(1, p.rect.rows - 1));
            const int width = std::max(1, p.rect.cols);
            std::size_t rowOffset = win.rowOff;
            if (r < rowOffset) rowOffset = r;
            if (r >= rowOffset + rows) rowOffset = r - rows + 1;
            int colOffset = win.colOff;
            if (column < colOffset) colOffset = column;
            if (column >= colOffset + width) colOffset = column - width + 1;

            w.rowOffset = rowOffset;
            w.colOffset = colOffset;
            w.line = r;
            w.col = static_cast<std::size_t>(column);
            w.cursorRow = static_cast<int>(r - rowOffset);
            w.cursorCol = column - colOffset;
        }
        v.windows.push_back(std::move(w));
    }
    return v;
}

void Editor::handleNormalKey(const Key& key) {
    const bool wasPendingG = pendingG_;
    pendingG_ = false;

    if (pendingWindow_) {
        pendingWindow_ = false;
        handleWindowKey(key);
        return;
    }

    if (pendingFind_ != 0) {
        const char kind = pendingFind_;
        pendingFind_ = 0;
        if (key.type == KeyType::Char) {
            startFind(kind, std::string(1, key.ch));
        } else if (key.type == KeyType::Tab) {
            startFind(kind, "\t");
        } else if (key.type == KeyType::Text) {
            startFind(kind, key.text);
        } else {
            resetPending();
        }
        return;
    }

    if (pendingReplace_) {
        pendingReplace_ = false;
        if (key.type == KeyType::Char) {
            replaceChars(std::string(1, key.ch), replaceCount_);
        } else if (key.type == KeyType::Text) {
            replaceChars(key.text, replaceCount_);
        }
        return;
    }

    if (pendingObject_ != 0) {
        const bool around = pendingObject_ == 'a';
        pendingObject_ = 0;
        if (key.type != KeyType::Char) {
            resetPending();
            return;
        }
        const int n = takeCount();
        const char op = pendingOp_;
        const std::optional<TextRange> range =
            textObject(*buffer_, {row_, col_}, key.ch, around, n);
        if (!range) {
            resetPending();
            return;
        }
        operateRange(op, *range);
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
            case '>':
            case '<':
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
            } else if (key.ch == 'w') {
                pendingWindow_ = true;
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
            if (const auto m = charLeft(*buffer_, here, n)) operate('d', *m);
            break;
        case 'x': deleteUnderCursor(n); break;
        case 'p': put(true, n); break;
        case 'P': put(false, n); break;
        case 'u': undo(n); break;
        case '.': repeatChange(n); break;
        case 'J': joinLines(n); break;
        case '~': toggleCase(n); break;
        case 'r':
            pendingReplace_ = true;
            replaceCount_ = n;
            break;
        case 'v': enterVisual('v'); break;
        case 'V': enterVisual('V'); break;
        case 'i':
            enterInsert();
            break;
        case 'a': {
            const std::string& line = buffer_->line(row_);
            col_ = line.empty() ? 0 : col_ + charLength(line, col_);
            enterInsert();
            break;
        }
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
            case 'i':
            case 'a':
                pendingObject_ = key.ch;
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
            rememberColumn();
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
        case '>': operateVisual('>', true); return;
        case '<': operateVisual('<', true); return;
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
        case KeyType::Char: insertString(std::string(1, key.ch)); break;
        case KeyType::Text: insertString(key.text); break;
        case KeyType::Tab: insertString("\t"); break;
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
        case KeyType::Text:
            commandText_ += key.text;
            break;
        case KeyType::Backspace:
            if (commandText_.empty()) {
                mode_ = Mode::Normal;
            } else {
                popLastChar(commandText_);
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
        case KeyType::Text:
            searchText_ += key.text;
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
                popLastChar(searchText_);
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
        if (windows_.size() > 1) {
            closeWindow();
        } else {
            requestQuit(force);
        }
    } else if (name == "qa" || name == "qall") {
        requestQuit(force);
    } else if (name == "wq" || name == "x") {
        if (save(argument)) {
            if (windows_.size() > 1) {
                closeWindow();
            } else {
                requestQuit(false, true);
            }
        }
    } else if (name == "wa" || name == "wall") {
        saveAll();
    } else if (name == "wqa" || name == "xa") {
        if (saveAll()) quit_ = true;
    } else if (name == "e" || name == "edit") {
        if (!argument.empty()) {
            edit(argument);
        } else if (path_.empty()) {
            message_ = "No file name";
        } else if (history_.modified() && !force) {
            message_ = "No write since last change (add ! to override)";
        } else {
            open(path_);
        }
    } else if (name == "bn" || name == "bnext" || name == "bp" || name == "bprev" ||
               name == "bprevious") {
        if (docs_.size() < 2) {
            message_ = "Only one buffer";
        } else {
            const bool forward = name == "bn" || name == "bnext";
            const std::size_t count = docs_.size();
            switchBuffer(forward ? (current_ + 1) % count : (current_ + count - 1) % count);
            message_ = bufferInfo();
        }
    } else if (name == "b" || name == "buffer") {
        gotoBuffer(argument);
    } else if (name == "ls" || name == "buffers") {
        listBuffers();
    } else if (name == "bd" || name == "bdelete") {
        closeBuffer(force);
    } else if (name == "sp" || name == "split") {
        splitWindow(false, argument);
    } else if (name == "vs" || name == "vsp" || name == "vsplit") {
        splitWindow(true, argument);
    } else if (name == "close" || name == "clo") {
        closeWindow();
    } else if (name == "only" || name == "on") {
        onlyWindow();
    } else if (name == "noh" || name == "nohlsearch") {
        highlight_ = false;
    } else if (name == "syntax") {
        if (argument == "on") {
            syntax_.setEnabled(true);
        } else if (argument == "off") {
            syntax_.setEnabled(false);
        } else if (argument.empty()) {
            message_ = syntax_.enabled() ? "syntax on" : "syntax off";
        } else {
            message_ = "usage: :syntax on|off";
        }
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

    if (path_.empty()) {
        path_ = target;
        syntax_.setFile(path_);
    }
    if (target == path_) history_.markSaved();
    message_ = describe(target, countLines(*buffer_, *bytes), *bytes) + " written";
    return true;
}

void Editor::requestQuit(bool force, bool skipCurrent) {
    if (!force) {
        const int index = modifiedBuffer(skipCurrent);
        if (index >= 0) {
            if (static_cast<std::size_t>(index) == current_) {
                message_ = "No write since last change (add ! to override)";
            } else {
                message_ = "No write since last change in buffer " +
                           std::to_string(index + 1) + " (add ! to override)";
            }
            return;
        }
    }
    quit_ = true;
}

std::string Editor::docPath(std::size_t index) const {
    return index == current_ ? path_ : docs_[index].path;
}

bool Editor::docModified(std::size_t index) const {
    return index == current_ ? history_.modified() : docs_[index].history.modified();
}

int Editor::modifiedBuffer(bool skipCurrent) const {
    for (std::size_t i = 0; i < docs_.size(); ++i) {
        if (skipCurrent && i == current_) continue;
        if (docModified(i)) return static_cast<int>(i);
    }
    return -1;
}

std::string Editor::bufferInfo() const {
    const std::string name = path_.empty() ? "[No Name]" : path_;
    return "buffer " + std::to_string(current_ + 1) + "/" + std::to_string(docs_.size()) +
           ": " + name;
}

void Editor::stash() {
    Doc& doc = docs_[current_];
    doc.buffer = std::move(buffer_);
    doc.history = std::move(history_);
    doc.path = path_;
    doc.row = row_;
    doc.col = col_;
    doc.wantedCol = wantedCol_;
    doc.rowOff = rowOff_;
    doc.colOff = colOff_;
}

void Editor::activate(std::size_t index) {
    Doc& doc = docs_[index];
    buffer_ = std::move(doc.buffer);
    history_ = std::move(doc.history);
    path_ = doc.path;
    row_ = doc.row;
    col_ = doc.col;
    wantedCol_ = doc.wantedCol;
    rowOff_ = doc.rowOff;
    colOff_ = doc.colOff;
    current_ = index;

    mode_ = Mode::Normal;
    blockInsert_.active = false;
    cmdKeys_.clear();
    resetPending();
    syntax_.setFile(path_);
    row_ = std::min(row_, buffer_->lineCount() - 1);
    col_ = snapToStart(buffer_->line(row_), std::min(col_, maxColumn(row_)));
}

void Editor::switchBuffer(std::size_t index) {
    if (index == current_ || index >= docs_.size()) return;
    stash();
    activate(index);
}

void Editor::selectBuffer(std::size_t index) {
    if (index >= docs_.size()) return;
    switchBuffer(index);
    message_ = bufferInfo();
    scroll();
}

void Editor::edit(const std::string& path) {
    if (path.empty()) {
        message_ = "usage: :e FILE";
        return;
    }

    for (std::size_t i = 0; i < docs_.size(); ++i) {
        if (docPath(i) == path) {
            switchBuffer(i);
            message_ = bufferInfo();
            scroll();
            return;
        }
    }

    const bool pristine = path_.empty() && !history_.modified() &&
                          buffer_->lineCount() == 1 && buffer_->line(0).empty();
    if (!pristine) {
        stash();
        Doc fresh;
        fresh.buffer = std::make_unique<LineBuffer>();
        docs_.push_back(std::move(fresh));
        activate(docs_.size() - 1);
    }

    open(path);

    if (path_.empty() && !pristine) {
        const std::string failure = message_;
        closeBuffer(true);
        message_ = failure;
    }
}

void Editor::closeBuffer(bool force) {
    if (history_.modified() && !force) {
        message_ = "No write since last change (add ! to override)";
        return;
    }

    if (docs_.size() == 1) {
        buffer_ = std::make_unique<LineBuffer>();
        history_.clear();
        path_.clear();
        row_ = 0;
        col_ = 0;
        wantedCol_ = 0;
        rowOff_ = 0;
        colOff_ = 0;
        mode_ = Mode::Normal;
        resetPending();
        syntax_.setFile("");
        message_ = "Buffer closed";
        return;
    }

    const std::size_t gone = current_;
    const std::size_t next = current_ + 1 < docs_.size() ? current_ : current_ - 1;
    docs_.erase(docs_.begin() + static_cast<std::ptrdiff_t>(current_));
    for (Window& w : windows_) {
        if (w.doc == gone) {
            w.doc = next;
        } else if (w.doc > gone) {
            --w.doc;
        }
    }
    activate(next);
    windows_[activeWin_].doc = current_;
    message_ = bufferInfo();
}

void Editor::gotoBuffer(const std::string& argument) {
    if (argument.empty()) {
        message_ = "usage: :b N|NAME";
        return;
    }

    const bool numeric = std::all_of(argument.begin(), argument.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });

    if (numeric) {
        const std::size_t n = argument.size() > 6 ? 0 : std::stoul(argument);
        if (n < 1 || n > docs_.size()) {
            message_ = "No buffer " + argument;
            return;
        }
        switchBuffer(n - 1);
        message_ = bufferInfo();
        return;
    }

    std::size_t found = 0;
    std::size_t matches = 0;
    for (std::size_t i = 0; i < docs_.size(); ++i) {
        if (docPath(i).find(argument) != std::string::npos) {
            found = i;
            ++matches;
        }
    }
    if (matches == 0) {
        message_ = "No matching buffer for " + argument;
    } else if (matches > 1) {
        message_ = "More than one match for " + argument;
    } else {
        switchBuffer(found);
        message_ = bufferInfo();
    }
}

void Editor::listBuffers() {
    std::string text;
    for (std::size_t i = 0; i < docs_.size(); ++i) {
        if (i > 0) text += "  ";
        text += std::to_string(i + 1);
        if (i == current_) text += '%';
        if (docModified(i)) text += '+';
        const std::string name = docPath(i);
        text += ' ';
        text += name.empty() ? "[No Name]" : name;
    }
    message_ = text;
}

bool Editor::saveAll() {
    std::size_t saved = 0;
    std::size_t skipped = 0;

    for (std::size_t i = 0; i < docs_.size(); ++i) {
        if (i == current_) {
            if (!history_.modified()) continue;
            if (!path_.empty() && save("")) {
                ++saved;
            } else {
                ++skipped;
            }
            continue;
        }

        Doc& doc = docs_[i];
        if (!doc.history.modified()) continue;
        if (doc.path.empty() || !saveFile(doc.path, *doc.buffer)) {
            ++skipped;
            continue;
        }
        doc.history.markSaved();
        ++saved;
    }

    message_ = std::to_string(saved) + (saved == 1 ? " buffer" : " buffers") + " written";
    if (skipped > 0) message_ += ", " + std::to_string(skipped) + " not written";
    return skipped == 0;
}

Rect Editor::screenArea() const {
    return {0, 0, std::max(1, size_.rows - 1), std::max(1, size_.cols)};
}

Rect Editor::activeRect() const {
    return layout_.rectOf(screenArea(), windows_[activeWin_].id).value_or(screenArea());
}

std::size_t Editor::indexOfWindow(int id) const {
    for (std::size_t i = 0; i < windows_.size(); ++i) {
        if (windows_[i].id == id) return i;
    }
    return activeWin_;
}

void Editor::saveWindow() {
    Window& w = windows_[activeWin_];
    w.doc = current_;
    w.row = row_;
    w.col = col_;
    w.wantedCol = wantedCol_;
    w.rowOff = rowOff_;
    w.colOff = colOff_;
}

void Editor::loadWindow(std::size_t index) {
    activeWin_ = index;
    const Window w = windows_[index];

    const std::size_t doc = std::min(w.doc, docs_.size() - 1);
    if (doc != current_) {
        stash();
        activate(doc);
    }

    mode_ = Mode::Normal;
    blockInsert_.active = false;
    cmdKeys_.clear();
    resetPending();

    row_ = std::min(w.row, buffer_->lineCount() - 1);
    col_ = snapToStart(buffer_->line(row_), std::min(w.col, maxColumn(row_)));
    wantedCol_ = w.wantedCol;
    rowOff_ = w.rowOff;
    colOff_ = w.colOff;
}

void Editor::focusWindow(std::size_t index) {
    if (index >= windows_.size() || index == activeWin_) return;
    saveWindow();
    loadWindow(index);
}

void Editor::cycleWindow(bool forward) {
    const std::vector<int> order = layout_.order();
    if (order.size() < 2) return;

    const int id = windows_[activeWin_].id;
    std::size_t pos = 0;
    while (pos < order.size() && order[pos] != id) ++pos;

    const std::size_t next = forward ? (pos + 1) % order.size()
                                     : (pos + order.size() - 1) % order.size();
    focusWindow(indexOfWindow(order[next]));
}

void Editor::splitWindow(bool sideBySide, const std::string& path) {
    const Rect area = activeRect();
    if (sideBySide ? area.cols < 21 : area.rows < 6) {
        message_ = "Not enough room to split";
        return;
    }

    saveWindow();
    const int id = layout_.split(windows_[activeWin_].id, sideBySide);
    if (id < 0) return;

    Window fresh = windows_[activeWin_];
    fresh.id = id;
    windows_.push_back(fresh);
    activeWin_ = windows_.size() - 1;

    if (!path.empty()) edit(path);
}

void Editor::closeWindow() {
    if (windows_.size() < 2) {
        message_ = "Cannot close last window";
        return;
    }

    const int id = windows_[activeWin_].id;
    const std::vector<int> order = layout_.order();
    std::size_t pos = 0;
    while (pos < order.size() && order[pos] != id) ++pos;
    const int target = order[pos > 0 ? pos - 1 : 1];

    layout_.close(id);
    windows_.erase(windows_.begin() + static_cast<std::ptrdiff_t>(activeWin_));
    loadWindow(indexOfWindow(target));
}

void Editor::onlyWindow() {
    if (windows_.size() < 2) return;

    saveWindow();
    const Window keep = windows_[activeWin_];
    layout_.only(keep.id);
    windows_.clear();
    windows_.push_back(keep);
    activeWin_ = 0;
}

void Editor::handleWindowKey(const Key& key) {
    takeCount();

    std::optional<Direction> direction;
    switch (key.type) {
        case KeyType::Left: direction = Direction::Left; break;
        case KeyType::Right: direction = Direction::Right; break;
        case KeyType::Up: direction = Direction::Up; break;
        case KeyType::Down: direction = Direction::Down; break;
        case KeyType::Ctrl:
            if (key.ch == 'w') cycleWindow(true);
            return;
        case KeyType::Char: break;
        default: return;
    }

    if (!direction) {
        switch (key.ch) {
            case 'h': direction = Direction::Left; break;
            case 'j': direction = Direction::Down; break;
            case 'k': direction = Direction::Up; break;
            case 'l': direction = Direction::Right; break;
            case 'w': cycleWindow(true); return;
            case 'W':
            case 'p': cycleWindow(false); return;
            case 'c':
            case 'q': closeWindow(); return;
            case 'o': onlyWindow(); return;
            case 's': splitWindow(false, ""); return;
            case 'v': splitWindow(true, ""); return;
            default: return;
        }
    }

    const std::optional<int> target =
        layout_.neighbor(screenArea(), windows_[activeWin_].id, *direction);
    if (target) focusWindow(indexOfWindow(*target));
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

Editor::BlockBounds Editor::blockBounds() const {
    auto edges = [&](Pos p) {
        const std::string& line = buffer_->line(p.row);
        const auto start = static_cast<std::size_t>(displayColumn(line, p.col));
        const std::size_t length = charLength(line, p.col);
        const auto finish = static_cast<std::size_t>(displayColumn(line, p.col + length));
        return std::pair<std::size_t, std::size_t>{start, finish > start ? finish - 1 : start};
    };

    const auto a = edges(anchor_);
    const auto b = edges({row_, col_});

    BlockBounds bounds;
    bounds.top = std::min(anchor_.row, row_);
    bounds.bottom = std::max(anchor_.row, row_);
    bounds.left = std::min(a.first, b.first);
    bounds.right = std::max(a.second, b.second);
    return bounds;
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

    const BlockBounds bounds = blockBounds();

    reg_.rows.clear();
    reg_.text.clear();
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    for (std::size_t r = bounds.top; r <= bounds.bottom; ++r) {
        const std::string& line = buffer_->line(r);
        const auto span = byteSpan(line, bounds.left, bounds.right);
        spans.push_back(span);
        reg_.rows.push_back(line.substr(span.first, span.second - span.first));
        if (r > bounds.top) reg_.text += '\n';
        reg_.text += reg_.rows.back();
    }
    reg_.linewise = false;
    reg_.block = true;

    mode_ = Mode::Normal;
    row_ = bounds.top;
    col_ = spans.front().first;

    if (op == 'y') {
        col_ = std::min(col_, maxColumn(row_));
        rememberColumn();
        return;
    }

    for (std::size_t r = bounds.top; r <= bounds.bottom; ++r) {
        const auto span = spans[r - bounds.top];
        if (span.second > span.first) applyEdit({r, span.first}, {r, span.second}, "");
    }

    if (op == 'c') {
        const std::size_t length = buffer_->line(bounds.top).size();
        col_ = std::min(spans.front().first, length);
        enterInsert();
        blockInsert_ = {true, bounds.top, bounds.bottom, col_, bounds.left, length};
        return;
    }

    col_ = std::min(col_, maxColumn(row_));
    rememberColumn();
}

void Editor::blockInsert(bool append) {
    takeCount();

    const BlockBounds bounds = blockBounds();
    const std::size_t dcol = append ? bounds.right + 1 : bounds.left;

    mode_ = Mode::Normal;
    row_ = bounds.top;
    const std::size_t length = buffer_->line(bounds.top).size();
    col_ = byteForColumn(buffer_->line(bounds.top), dcol);
    enterInsert();
    blockInsert_ = {true, bounds.top, bounds.bottom, col_, dcol, length};
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
        const std::string& other = buffer_->line(r);
        if (static_cast<std::size_t>(displayWidth(other)) < block.dcol) continue;
        const std::size_t at = byteForColumn(other, block.dcol);
        applyEdit({r, at}, {r, at}, typed);
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
        const BlockBounds bounds = blockBounds();
        const Register previous = reg_;
        operateVisual('d', false);
        const Register deleted = reg_;

        reg_ = previous;
        const auto width = static_cast<std::size_t>(displayWidth(buffer_->line(row_)));
        put(width <= bounds.left, n);
        reg_ = deleted;
        return;
    }

    const bool linewise = visualKind_ == 'V';
    const bool atEnd = linewise ? b.row + 1 >= buffer_->lineCount()
                                : b.col + charLength(buffer_->line(b.row), b.col) >=
                                      buffer_->line(b.row).size();

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
    rememberColumn();
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
    rememberColumn();
}

void Editor::commitSearch() {
    mode_ = Mode::Normal;
    const std::string pattern = searchText_;
    searchText_.clear();

    row_ = searchOrigin_.row;
    col_ = searchOrigin_.col;
    rememberColumn();

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
    rememberColumn();
}

void Editor::leaveInsert() {
    if (blockInsert_.active) finishBlockInsert();
    mode_ = Mode::Normal;
    if (col_ > 0) col_ = prevCharStart(buffer_->line(row_), col_);
    col_ = std::min(col_, maxColumn(row_));
    rememberColumn();
}

void Editor::insertString(const std::string& text) {
    applyEdit({row_, col_}, {row_, col_}, text);
    col_ += text.size();
    rememberColumn();
}

void Editor::insertNewline() {
    applyEdit({row_, col_}, {row_, col_}, "\n");
    ++row_;
    col_ = 0;
    wantedCol_ = 0;
}

void Editor::backspace() {
    if (col_ > 0) {
        const std::size_t previous = prevCharStart(buffer_->line(row_), col_);
        applyEdit({row_, previous}, {row_, col_}, "");
        col_ = previous;
    } else if (row_ > 0) {
        const std::size_t previousLength = buffer_->line(row_ - 1).size();
        applyEdit({row_ - 1, previousLength}, {row_, 0}, "");
        --row_;
        col_ = previousLength;
    } else {
        return;
    }
    rememberColumn();
}

void Editor::deleteForward() {
    const std::string& line = buffer_->line(row_);
    const std::size_t length = line.size();
    if (col_ < length) {
        applyEdit({row_, col_}, {row_, col_ + charLength(line, col_)}, "");
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
    changed_ = true;
    syntax_.invalidate(start.row);
    history_.record(buffer_->replace(start, end, text));
}

void Editor::undo(int count) {
    std::optional<Pos> where;
    for (int i = 0; i < count; ++i) {
        const std::optional<Pos> next = history_.undo(*buffer_);
        if (!next) break;
        where = next;
    }
    syntax_.invalidate(0);
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
    syntax_.invalidate(0);
    if (where) {
        placeCursor(*where);
    } else {
        message_ = "Already at newest change";
    }
}

void Editor::placeCursor(Pos where) {
    row_ = std::min(where.row, buffer_->lineCount() - 1);
    col_ = std::min(where.col, maxColumn(row_));
    col_ = snapToStart(buffer_->line(row_), col_);
    rememberColumn();
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
    pendingObject_ = 0;
    pendingReplace_ = false;
    pendingWindow_ = false;
    pendingG_ = false;
    count_ = 0;
    opCount_ = 0;
}

std::string Editor::pendingText() const {
    std::string text;
    if (opCount_ > 0) text += std::to_string(opCount_);
    if (pendingOp_ != 0) text += pendingOp_;
    if (count_ > 0) text += std::to_string(count_);
    if (pendingObject_ != 0) text += pendingObject_;
    if (pendingFind_ != 0) text += pendingFind_;
    if (pendingReplace_) text += 'r';
    if (pendingWindow_) text += "^W";
    if (pendingG_) text += 'g';
    return text;
}

std::optional<Motion> Editor::operatorMotion(const Key& key, bool wasPendingG, int count,
                                             bool counted) const {
    const Pos here{row_, col_};

    switch (key.type) {
        case KeyType::Left: return charLeft(*buffer_, here, count);
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
        case 'h': return charLeft(*buffer_, here, count);
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
    operateRange(op, makeRange(*buffer_, {row_, col_}, motion));
}

void Editor::operateRange(char op, const TextRange& range) {
    pendingOp_ = 0;

    if (op == '>' || op == '<') {
        shiftLines(op == '>', range.start.row, range.end.row);
        return;
    }

    if (!range.linewise && range.start == range.end) {
        if (op == 'c') {
            row_ = range.start.row;
            col_ = range.start.col;
            enterInsert();
        }
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
        rememberColumn();
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
        rememberColumn();
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
    col_ = snapToStart(buffer_->line(row_), col_);
    rememberColumn();
}

void Editor::shiftLines(bool right, std::size_t first, std::size_t last) {
    last = std::min(last, buffer_->lineCount() - 1);
    const auto width = static_cast<std::size_t>(kTabStop);

    for (std::size_t r = first; r <= last; ++r) {
        const std::string& line = buffer_->line(r);
        if (right) {
            if (line.empty()) continue;
            applyEdit({r, 0}, {r, 0}, std::string(width, ' '));
        } else {
            std::size_t n = 0;
            if (!line.empty() && line[0] == '\t') {
                n = 1;
            } else {
                while (n < line.size() && n < width && line[n] == ' ') ++n;
            }
            if (n > 0) applyEdit({r, 0}, {r, n}, "");
        }
    }

    row_ = first;
    col_ = firstNonBlank(*buffer_, {row_, 0}).target.col;
    col_ = std::min(col_, maxColumn(row_));
    rememberColumn();
}

void Editor::joinLines(int count) {
    const int joins = std::max(count, 2) - 1;
    std::size_t where = col_;
    bool any = false;

    for (int i = 0; i < joins && row_ + 1 < buffer_->lineCount(); ++i) {
        const std::string& cur = buffer_->line(row_);
        const std::string& next = buffer_->line(row_ + 1);
        const std::size_t lead = std::min(next.find_first_not_of(" \t"), next.size());
        const std::size_t curLen = cur.size();
        const bool nextEmpty = lead >= next.size();
        const bool needSpace = !nextEmpty && !cur.empty() && cur.back() != ' ' &&
                               cur.back() != '\t' && next[lead] != ')';

        applyEdit({row_, curLen}, {row_ + 1, lead}, needSpace ? " " : "");
        where = curLen;
        any = true;
    }
    if (!any) return;

    col_ = std::min(where, maxColumn(row_));
    col_ = snapToStart(buffer_->line(row_), col_);
    rememberColumn();
}

void Editor::toggleCase(int count) {
    const Pos here{row_, col_};
    const std::optional<Motion> motion = charRight(*buffer_, here, count);
    if (!motion) return;

    std::string text = buffer_->text(here, motion->target);
    for (char& c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 128) continue;
        if (std::isupper(u) != 0) {
            c = static_cast<char>(std::tolower(u));
        } else if (std::islower(u) != 0) {
            c = static_cast<char>(std::toupper(u));
        }
    }
    applyEdit(here, motion->target, text);

    col_ = std::min(motion->target.col, maxColumn(row_));
    col_ = snapToStart(buffer_->line(row_), col_);
    rememberColumn();
}

void Editor::replaceChars(const std::string& with, int count) {
    const std::string& line = buffer_->line(row_);
    std::size_t end = col_;
    for (int i = 0; i < count; ++i) {
        if (end >= line.size()) return;
        end += charLength(line, end);
    }

    std::string payload;
    for (int i = 0; i < count; ++i) payload += with;
    applyEdit({row_, col_}, {row_, end}, payload);

    col_ += static_cast<std::size_t>(count - 1) * with.size();
    col_ = std::min(col_, maxColumn(row_));
    col_ = snapToStart(buffer_->line(row_), col_);
    rememberColumn();
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

        const std::string& current = buffer_->line(row_);
        const std::size_t length = current.size();
        const std::size_t at =
            after && length > 0 ? std::min(col_ + charLength(current, col_), length) : col_;
        applyEdit({row_, at}, {row_, at}, payload);
        if (payload.find('\n') == std::string::npos) {
            col_ = at + prevCharStart(payload, payload.size());
        } else {
            col_ = at;
        }
    }

    col_ = std::min(col_, maxColumn(row_));
    rememberColumn();
}

void Editor::putBlock(bool after, int count) {
    std::size_t width = 0;
    for (const std::string& piece : reg_.rows) {
        width = std::max(width, static_cast<std::size_t>(displayWidth(piece)));
    }

    const std::string& current = buffer_->line(row_);
    const std::size_t length = current.size();
    const std::size_t at =
        after && length > 0 ? std::min(col_ + charLength(current, col_), length) : col_;
    const auto atCol = static_cast<std::size_t>(displayColumn(current, at));
    const std::size_t first = row_;

    for (std::size_t i = 0; i < reg_.rows.size(); ++i) {
        const std::string& source = reg_.rows[i];
        const std::size_t gap = width - static_cast<std::size_t>(displayWidth(source));

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
            applyEdit({lastRow, end}, {lastRow, end}, "\n" + std::string(atCol, ' ') + piece);
            continue;
        }

        const std::string& line = buffer_->line(r);
        const auto lineWidth = static_cast<std::size_t>(displayWidth(line));
        if (lineWidth <= atCol) piece.resize(piece.size() - gap);

        const std::size_t position = byteForColumn(line, atCol);
        const std::size_t padding = atCol > lineWidth ? atCol - lineWidth : 0;
        applyEdit({r, position}, {r, position}, std::string(padding, ' ') + piece);
    }

    row_ = first;
    col_ = std::min(at, maxColumn(row_));
    col_ = snapToStart(buffer_->line(row_), col_);
    rememberColumn();
}

void Editor::moveLeft(std::size_t n) {
    const std::string& line = buffer_->line(row_);
    for (std::size_t i = 0; i < n && col_ > 0; ++i) col_ = prevCharStart(line, col_);
    rememberColumn();
}

void Editor::moveRight(std::size_t n) {
    const std::string& line = buffer_->line(row_);
    const std::size_t limit = maxColumn(row_);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t length = charLength(line, col_);
        if (length == 0 || col_ + length > limit) break;
        col_ += length;
    }
    rememberColumn();
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
    col_ = snapToStart(buffer_->line(row_), col_);
    rememberColumn();
}

void Editor::startFind(char kind, const std::string& target) {
    FindState find;
    find.target = target;
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
        const std::string& line = buffer_->line(row_);
        if (find.forward) {
            start.col += charLength(line, start.col);
        } else if (start.col > 0) {
            start.col = prevCharStart(line, start.col);
        } else {
            resetPending();
            return;
        }
    }

    const std::optional<Motion> motion =
        findChar(*buffer_, start, find.target, find.forward, find.till, count);

    if (motion && pendingOp_ != 0) {
        operate(pendingOp_, *motion);
        return;
    }
    resetPending();
    if (motion) jumpTo(*motion);
}

void Editor::rememberColumn() {
    wantedCol_ = static_cast<std::size_t>(renderColumn());
}

void Editor::applyWantedColumn() {
    const std::string& line = buffer_->line(row_);
    if (wantedCol_ == kEnd) {
        col_ = maxColumn(row_);
    } else {
        col_ = std::min(byteForColumn(line, wantedCol_), maxColumn(row_));
    }
    col_ = snapToStart(line, col_);
}

void Editor::scroll() {
    const std::size_t rows = textRows();
    if (row_ < rowOff_) rowOff_ = row_;
    if (row_ >= rowOff_ + rows) rowOff_ = row_ - rows + 1;

    const int width = std::max(1, activeRect().cols);
    const int column = renderColumn();
    if (column < colOff_) colOff_ = column;
    if (column >= colOff_ + width) colOff_ = column - width + 1;
}

std::size_t Editor::maxColumn(std::size_t row) const {
    const std::string& line = buffer_->line(row);
    if (line.empty()) return 0;
    if (mode_ == Mode::Insert) return line.size();
    return prevCharStart(line, line.size());
}

std::size_t Editor::textRows() const {
    return static_cast<std::size_t>(std::max(1, activeRect().rows - 1));
}

int Editor::renderColumn() const {
    return displayColumn(buffer_->line(row_), col_);
}

}