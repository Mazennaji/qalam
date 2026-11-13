#include "editor/editor.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>
#include <cstdint>

#include "buffer/file_io.h"
#include "buffer/line_buffer.h"
#include "clipboard/clipboard.h"
#include "input/key_notation.h"
#include "search/substitute.h"
#include "text/columns.h"
#include "finder/fuzzy.h"
#include "text/bidi.h"
#include "editor/help.h"

namespace qalam {
    namespace {
        constexpr std::size_t kEnd = std::numeric_limits<std::size_t>::max();
        constexpr int kMaxCount = 99999;
        constexpr int kMaxMacroDepth = 50;
        constexpr int kMacroBudget = 200000;

        char lowerChar(char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        std::string keyLabel(const Key &key) {
            switch (key.type) {
                case KeyType::Char: return std::string(1, key.ch);
                case KeyType::Text: return key.text;
                case KeyType::Ctrl: return std::string("<C-") + key.ch + ">";
                default: return "<" + keyName(key) + ">";
            }
        }

        std::string preview(const std::string &text) {
            std::string flat;
            for (char c: text) {
                if (c == '\n') {
                    flat += "^J";
                } else if (c == '\t') {
                    flat += "^I";
                } else {
                    flat += c;
                }
            }
            const std::string cut = truncateToWidth(flat, 24);
            return cut.size() < flat.size() ? cut + "..." : cut;
        }

        struct SubstituteSpec {
            std::size_t first = 0;
            std::size_t last = 0;
            std::string pattern;
            std::string replacement;
            bool global = false;
            CaseMode mode = CaseMode::Smart;
        };

        enum class Parse { NotSubstitute, Valid, Invalid };

        std::string trim(const std::string &s) {
            const auto first = s.find_first_not_of(" \t");
            if (first == std::string::npos) return "";
            const auto last = s.find_last_not_of(" \t");
            return s.substr(first, last - first + 1);
        }

        std::size_t countLines(const TextBuffer &buffer, std::size_t bytes) {
            return bytes == 0 ? 0 : buffer.lineCount();
        }

        std::string describe(const std::string &path, std::size_t lines, std::size_t bytes) {
            return "\"" + path + "\" " + std::to_string(lines) + "L, " + std::to_string(bytes) + "B";
        }

        void remember(std::vector<std::string> &history, const std::string &text) {
            if (text.empty()) return;
            if (!history.empty() && history.back() == text) return;
            history.push_back(text);
            if (history.size() > 100) history.erase(history.begin());
        }

        bool takesFile(const std::string &name) {
            static const char *const names[] = {
                "e", "edit", "w", "write", "sp", "split", "vs",
                "vsp", "vsplit", "r", "read", "wq", "x"
            };
            return std::any_of(std::begin(names), std::end(names),
                               [&](const char *n) { return name == n; });
        }

        std::string commonPrefix(const std::vector<std::string> &items) {
            std::string prefix = items.front();
            for (const std::string &s: items) {
                std::size_t i = 0;
                while (i < prefix.size() && i < s.size() && prefix[i] == s[i]) ++i;
                prefix.resize(i);
            }
            return prefix;
        }

        std::vector<std::string> fileCandidates(const std::string &typed) {
            namespace fs = std::filesystem;

            const std::size_t slash = typed.rfind('/');
            const std::string dir = slash == std::string::npos ? "" : typed.substr(0, slash + 1);
            const std::string stem = slash == std::string::npos ? typed : typed.substr(slash + 1);

            std::vector<std::string> out;
            std::error_code ec;
            fs::directory_iterator it(dir.empty() ? "." : dir, ec);
            if (ec) return out;

            for (; it != fs::directory_iterator(); it.increment(ec)) {
                if (ec) break;
                const std::string name = it->path().filename().string();
                if (name.compare(0, stem.size(), stem) != 0) continue;
                if (name[0] == '.' && (stem.empty() || stem[0] != '.')) continue;

                std::error_code probe;
                const bool isDir = it->is_directory(probe);
                out.push_back(dir + name + (isDir ? "/" : ""));
                if (out.size() >= 200) break;
            }
            std::sort(out.begin(), out.end());
            return out;
        }

        std::string positionKey(const std::string &path) {
            std::error_code ec;
            const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
            if (ec) return path;
            return absolute.lexically_normal().string();
        }

        struct ViewPlace {
            std::size_t rowOff = 0;
            int colOff = 0;
            int cursorRow = 0;
            int cursorCol = 0;
        };

        std::string builtinAlias(const std::string &name) {
            static const std::pair<const char *, const char *> table[] = {
                {"خلص", "q"},
                {"احفظ", "w"},
                {"احفظ_وخلص", "wq"},
                {"افتح", "e"},
                {"اخرج", "qa"},
            };
            for (const auto &entry: table) {
                if (name == entry.first) return entry.second;
            }
            return name;
        }

        int visualColumn(const std::string &line, std::size_t col, bool bidi) {
            const int logical = displayColumn(line, col);
            if (!bidi) return logical;
            const std::vector<std::size_t> order = visualCellOrder(line);
            for (std::size_t v = 0; v < order.size(); ++v) {
                if (order[v] == static_cast<std::size_t>(logical)) return static_cast<int>(v);
            }
            return logical;
        }

        ViewPlace placeInView(const TextBuffer &text, std::size_t row, std::size_t col,
                              std::size_t rowOff, int colOff, int rows, int width, bool wrap, bool bidi) {
            const std::string &line = text.line(row);
            const int column = visualColumn(line, col, bidi);
            if (rowOff > row) rowOff = row;

            if (wrap) {
                const int segment = column / width;
                int used = segment + 1;
                std::size_t top = row;
                while (top > 0) {
                    const int h = wrapHeight(text.line(top - 1), width);
                    if (used + h > rows) break;
                    used += h;
                    --top;
                }
                if (rowOff < top) rowOff = top;

                int above = 0;
                for (std::size_t r = rowOff; r < row; ++r) above += wrapHeight(text.line(r), width);
                return {rowOff, 0, above + segment, column % width};
            }

            const auto height = static_cast<std::size_t>(rows);
            if (row >= rowOff + height) rowOff = row - height + 1;
            if (column < colOff) colOff = column;
            if (column >= colOff + width) colOff = column - width + 1;
            return {rowOff, colOff, static_cast<int>(row - rowOff), column - colOff};
        }

        std::optional<MapMode> mapModeFor(Mode mode) {
            switch (mode) {
                case Mode::Normal: return MapMode::Normal;
                case Mode::Insert: return MapMode::Insert;
                case Mode::Visual: return MapMode::Visual;
                case Mode::Command:
                case Mode::Search: return std::nullopt;
                case Mode::Picker: return std::nullopt;
            }
            return std::nullopt;
        }

        TextRange makeRange(const TextBuffer &buffer, Pos from, const Motion &motion) {
            Pos a = from;
            Pos b = motion.target;
            if (b < a) std::swap(a, b);
            if (motion.linewise) return {a, b, true};

            if (motion.inclusive) {
                const std::string &line = buffer.line(b.row);
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

        std::optional<std::size_t> parseAddress(const std::string &s, std::size_t &i,
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

        Parse parseSubstitute(const std::string &text, std::size_t current, std::size_t lineCount,
                              SubstituteSpec &spec) {
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

            auto readPart = [&](std::string &out) {
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

    void Editor::open(const std::string &path) {
        const LoadResult result = loadFile(path, *buffer_);
        switch (result.status) {
            case LoadStatus::Loaded:
                path_ = path;
                crlf_ = result.crlf;
                message_ = describe(path, countLines(*buffer_, result.bytes), result.bytes);
                if (result.crlf) message_ += " [dos]";
                break;
            case LoadStatus::NotFound:
                path_ = path;
                crlf_ = false;
                message_ = "\"" + path + "\" [New]";
                break;
            case LoadStatus::Failed:
                message_ = "Cannot read \"" + path + "\"";
                break;
        }
        filetype_.clear();
        refreshSyntax();
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
        marks_.clear();
        jumps_.clear();
        jumpIdx_ = 0;
        scroll();
    }

    bool Editor::useConfig(const std::string &path) {
        configPath_ = path;
        const Config config = loadConfig(path);

        keymap_ = Keymap();
        for (const Mapping &mapping: config.mappings) {
            keymap_.add(mapping.mode, mapping.lhs, mapping.rhs);
        }
        aliases_ = config.aliases;
        syntax_.resetColors();
        for (const auto &[name, code]: config.colors) syntax_.setColor(name, code);
        syntax_.setEnabled(config.syntax);
        number_ = config.number;
        relative_ = config.relativeNumber;
        wrap_ = config.wrap;
        bidi_ = config.bidi;
        mouse_ = config.mouse;
        setTabStop(config.tabstop);
        shiftwidth_ = config.shiftwidth;
        expandtab_ = config.expandtab;
        autoindent_ = config.autoindent;
        historyOn_ = config.history;
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

    void Editor::loadHistory(const std::string &path) {
        historyPath_ = path;
        if (!historyOn_ || path.empty()) return;

        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            if (line.size() < 2) continue;
            if (line[0] == ':') {
                remember(cmdHist_, line.substr(1));
            } else if (line[0] == '/') {
                remember(searchHist_, line.substr(1));
            } else if (line[0] == '@') {
                const std::size_t a = line.find(':', 1);
                const std::size_t b = a == std::string::npos ? a : line.find(':', a + 1);
                if (b == std::string::npos) continue;
                const std::string rowText = line.substr(1, a - 1);
                const std::string colText = line.substr(a + 1, b - a - 1);
                const auto digits = [](const std::string &s) {
                    return !s.empty() && s.size() <= 9 &&
                           std::all_of(s.begin(), s.end(), [](char c) {
                               return std::isdigit(static_cast<unsigned char>(c)) != 0;
                           });
                };
                if (!digits(rowText) || !digits(colText)) continue;
                positions_.emplace_back(line.substr(b + 1),
                                        Pos{std::stoul(rowText), std::stoul(colText)});
            }
        }

        restorePosition();
        restoreUndo();
    }

    void Editor::restorePosition() {
        if (!historyOn_ || path_.empty()) return;

        const std::string key = positionKey(path_);
        for (auto it = positions_.rbegin(); it != positions_.rend(); ++it) {
            if (it->first == key) {
                placeCursor(it->second);
                scroll();
                return;
            }
        }
    }

    std::string Editor::undoFile() const {
        if (!historyOn_ || historyPath_.empty() || path_.empty()) return "";
        char name[17];
        std::snprintf(name, sizeof name, "%016llx",
                      static_cast<unsigned long long>(fnv1a(positionKey(path_))));
        const std::filesystem::path dir = std::filesystem::path(historyPath_).parent_path() / "undo";
        return (dir / name).string();
    }

    void Editor::writeUndo() {
        const std::string file = undoFile();
        if (file.empty()) return;

        history_.close();
        const std::string body = history_.serialize();
        if (body.size() > (std::size_t{32} << 20)) return;

        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);

        const std::string temp = file + ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) return;
            out << "QUNDO1\nH " << fnv1a(bufferText()) << "\nB " << body.size() << "\n"
                << body << "C " << fnv1a(body) << "\n";
            if (!out) return;
        }
        std::filesystem::rename(temp, file, ec);
        if (ec) std::filesystem::remove(temp, ec);
    }

    void Editor::restoreUndo() {
        const std::string file = undoFile();
        if (file.empty() || history_.modified()) return;

        std::ifstream in(file, std::ios::binary);
        if (!in) return;
        const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        std::size_t pos = 0;
        auto line = [&](std::string& out) {
            const std::size_t end = data.find('\n', pos);
            if (end == std::string::npos) return false;
            out = data.substr(pos, end - pos);
            pos = end + 1;
            return true;
        };

        std::string magic, hashLine, sizeLine;
        if (!line(magic) || magic != "QUNDO1" || !line(hashLine) || !line(sizeLine)) return;
        if (hashLine.rfind("H ", 0) != 0 || sizeLine.rfind("B ", 0) != 0) return;

        std::uint64_t wanted = 0;
        std::size_t size = 0;
        try {
            wanted = std::stoull(hashLine.substr(2));
            size = std::stoull(sizeLine.substr(2));
        } catch (...) {
            return;
        }
        if (wanted != fnv1a(bufferText()) || size > data.size() - pos) return;

        const std::string body = data.substr(pos, size);
        pos += size;
        std::string trailer;
        if (!line(trailer) || trailer != "C " + std::to_string(fnv1a(body))) return;

        history_.deserialize(body);
    }

    void Editor::saveHistory() {
        if (!historyOn_ || historyPath_.empty()) return;

        for (std::size_t i = 0; i < docs_.size(); ++i) {
            const std::string name = docPath(i);
            if (name.empty()) continue;
            const std::string key = positionKey(name);
            const Pos where = i == current_ ? Pos{row_, col_} : Pos{docs_[i].row, docs_[i].col};
            positions_.erase(std::remove_if(positions_.begin(), positions_.end(),
                                            [&](const auto &e) { return e.first == key; }),
                             positions_.end());
            positions_.emplace_back(key, where);
        }
        if (positions_.size() > 100) {
            positions_.erase(positions_.begin(),
                             positions_.begin() + static_cast<std::ptrdiff_t>(positions_.size() - 100));
        }

        std::error_code ec;
        const std::filesystem::path file(historyPath_);
        if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);

        std::ofstream out(historyPath_, std::ios::trunc);
        if (!out) return;
        for (const std::string &entry: cmdHist_) out << ':' << entry << '\n';
        for (const std::string &entry: searchHist_) out << '/' << entry << '\n';
        for (const auto &entry: positions_) {
            out << '@' << entry.second.row << ':' << entry.second.col << ':' << entry.first << '\n';
        }
    }

    void Editor::resize(Size size) {
        size_ = size;
        scroll();
    }

    void Editor::handleKey(const Key &key) {

        if (key.type == KeyType::Mouse) {
            handleMouse(key);
            return;
        }

        if (key.type == KeyType::Paste) {
            handlePaste(key.text);
            return;
        }
        mapPending_.push_back(key);
        mapTicks_ = 0;
        resolveMaps(false);
    }

    void Editor::handlePaste(const std::string &raw) {
        mapPending_.clear();
        mapTicks_ = 0;

        std::string text;
        text.reserve(raw.size());
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\r') {
                if (i + 1 < raw.size() && raw[i + 1] == '\n') ++i;
                text += '\n';
            } else {
                text += raw[i];
            }
        }
        if (text.empty()) return;

        switch (mode_) {
            case Mode::Insert: {
                const Pos start{row_, col_};
                applyEdit(start, start, text);
                const Pos end = endOf(start, text);
                row_ = end.row;
                col_ = end.col;
                rememberColumn();
                tainted_ = true;
                break;
            }
            case Mode::Command:
            case Mode::Search: {
                std::replace(text.begin(), text.end(), '\n', ' ');
                insertPrompt(text);
                if (mode_ == Mode::Search) previewSearch();
                break;
            }
            case Mode::Normal:
            case Mode::Visual:
                message_ = "Paste ignored: press i first, or use \"+p";
                break;
            case Mode::Picker: {
                std::replace(text.begin(), text.end(), '\n', ' ');
                pickQuery_ += text;
                pickSel_ = 0;
                refreshPicker();
                break;
            }
        }
        scroll();
    }

    Pos Editor::screenToPos(int x, int y) const {
        const Rect r = activeRect();
        const int textRowsHere = std::max(1, r.rows - 1);
        const int cy = std::clamp(y - r.row, 0, textRowsHere - 1);
        const int g = gutter();
        const int tw = textWidth();
        const int rx = std::clamp(x - r.col - g, 0, std::max(0, tw - 1));
        const std::size_t count = buffer_->lineCount();

        std::size_t row = rowOff_;
        int cell = 0;
        if (wrap_) {
            int acc = 0;
            int seg = 0;
            for (;;) {
                const int h = wrapHeight(buffer_->line(row), tw);
                if (cy < acc + h) {
                    seg = cy - acc;
                    break;
                }
                if (row + 1 >= count) {
                    seg = h - 1;
                    break;
                }
                acc += h;
                ++row;
            }
            cell = seg * tw + rx;
        } else {
            row = std::min(rowOff_ + static_cast<std::size_t>(cy), count - 1);
            cell = colOff_ + rx;
        }

        const std::string &line = buffer_->line(row);
        auto logical = static_cast<std::size_t>(std::max(cell, 0));
        if (bidi_) {
            const std::vector<std::size_t> order = visualCellOrder(line);
            if (!order.empty() && logical < order.size()) logical = order[logical];
        }
        return {row, byteForColumn(line, logical)};
    }

    void Editor::handleMouse(const Key &key) {
        if (!mouse_) return;
        if (mode_ == Mode::Command || mode_ == Mode::Search || mode_ == Mode::Picker) return;

        const bool wheel = (key.button & 64) != 0;
        const bool drag = (key.button & 32) != 0;
        const int base = key.button & 3;

        if (wheel) {
            if (base == 0) {
                moveUp(3);
            } else if (base == 1) {
                moveDown(3);
            }
            scroll();
            return;
        }
        if (base != 0) return;

        if (drag) {
            if (!mouseDown_) return;
            const Pos p = screenToPos(key.x, key.y);
            if (mode_ == Mode::Normal) enterVisual('v');
            if (mode_ == Mode::Visual) placeCursor(p);
            scroll();
            return;
        }

        if (!key.press) {
            mouseDown_ = false;
            return;
        }

        const std::vector<Placement> placed = layout_.place(screenArea(), nullptr);
        for (const Placement &p: placed) {
            const Rect &r = p.rect;
            if (key.y < r.row || key.y >= r.row + r.rows) continue;
            if (key.x < r.col || key.x >= r.col + r.cols) continue;
            const std::size_t index = indexOfWindow(p.id);
            if (index != activeWin_) focusWindow(index);
            break;
        }

        if (mode_ == Mode::Visual) leaveVisual();
        if (mode_ == Mode::Insert) history_.close();
        mouseDown_ = true;
        placeCursor(screenToPos(key.x, key.y));
        scroll();
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
                return !anyPending() && selReg_ == 0;
            case Mode::Visual:
                return pendingFind_ == 0 && pendingMark_ == 0 && !pendingRegister_ &&
                       selReg_ == 0 && !pendingG_ && count_ == 0;
            case Mode::Command:
            case Mode::Search:
            case Mode::Picker:
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
                    for (const Key &key: rhs) dispatch(key);
                    return;
                }
            }

            const Key first = mapPending_.front();
            mapPending_.erase(mapPending_.begin());
            dispatch(first);
        }
    }

    void Editor::dispatch(const Key &key) {
        if (!replaying_) cmdKeys_.push_back(key);
        if (recording_ != 0 && !replaying_ && macroDepth_ == 0) recordKeys_.push_back(key);
        const bool hadRegister = selReg_ != 0;

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
            case Mode::Picker:
                handlePickerKey(key);
                break;
        }
        if (hadRegister && selReg_ != 0 && !anyPending()) selReg_ = 0;
        if (mode_ != Mode::Insert && batch_ == 0) history_.close();
        if (!replaying_) trackRepeat();
        scroll();
    }

    void Editor::trackRepeat() {
        if (mode_ == Mode::Visual || mode_ == Mode::Command || mode_ == Mode::Search || mode_ == Mode::Picker) {
            tainted_ = true;
            cmdKeys_.clear();
            return;
        }
        if (mode_ == Mode::Insert) return;
        if (anyPending() || selReg_ != 0) return;

        if (changed_ && !tainted_ && !cmdKeys_.empty()) lastChange_ = cmdKeys_;
        cmdKeys_.clear();
        changed_ = false;
        tainted_ = false;
    }

    void Editor::repeatChange(int count) {
        if (lastChange_.empty()) return;

        const std::vector<Key> keys = lastChange_;
        const bool before = replaying_;
        replaying_ = true;
        for (int i = 0; i < count; ++i) {
            for (const Key &key: keys) dispatch(key);
        }
        replaying_ = before;

        cmdKeys_.clear();
        changed_ = false;
        tainted_ = false;
    }

    bool Editor::shouldQuit() const {
        return quit_;
    }

    bool Editor::mouseEnabled() const {
        return mouse_;
    }

    void Editor::setText(const std::string &text) {
        const std::size_t last = buffer_->lineCount() - 1;
        static_cast<void>(buffer_->replace({0, 0}, {last, buffer_->line(last).size()}, text));
        history_.clear();
        marks_.clear();
        jumps_.clear();
        jumpIdx_ = 0;
        mode_ = Mode::Normal;
        row_ = 0;
        col_ = 0;
        wantedCol_ = 0;
        rowOff_ = 0;
        colOff_ = 0;
        lastChange_.clear();
        cmdKeys_.clear();
        changed_ = false;
        tainted_ = false;
        resetPending();
        syntax_.invalidate(0);
        scroll();
    }

    std::string Editor::bufferText() const {
        const std::size_t last = buffer_->lineCount() - 1;
        return buffer_->text({0, 0}, {last, buffer_->line(last).size()});
    }

    Pos Editor::cursor() const {
        return {row_, col_};
    }

    Mode Editor::mode() const {
        return mode_;
    }

    const std::string &Editor::message() const {
        return message_;
    }

    ViewState Editor::view() const {
        ViewState v;

        std::string modeName;
        switch (mode_) {
            case Mode::Normal: modeName = "NORMAL";
                break;
            case Mode::Insert: modeName = "INSERT";
                break;
            case Mode::Command: modeName = "COMMAND";
                break;
            case Mode::Search: modeName = "SEARCH";
                break;
            case Mode::Visual:
                modeName = visualKind_ == 'V' ? "V-LINE" : visualKind_ == 'B' ? "V-BLOCK" : "VISUAL";
                break;
            case Mode::Picker:
                modeName = "PICK";
                break;
        }
        if (recording_ != 0) modeName += std::string(" rec @") + recording_;

        v.commandLine = mode_ == Mode::Command || mode_ == Mode::Search;
        if (mode_ == Mode::Command) {
            v.promptCursor =
                    1 + displayWidth(commandText_.substr(0, std::min(promptPos_, commandText_.size())));
        } else if (mode_ == Mode::Search) {
            v.promptCursor =
                    1 + displayWidth(searchText_.substr(0, std::min(promptPos_, searchText_.size())));
        }
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
        }
        if (mode_ == Mode::Picker) {
            const std::string prompt = (pickBuffers_ ? "buffer> " : "file> ") + pickQuery_;
            v.commandLine = true;
            v.message = prompt;
            v.promptCursor = displayWidth(prompt);

            const auto maxLines = static_cast<std::size_t>(std::max(1, size_.rows - 3));
            if (pickHits_.empty()) {
                v.overlay.push_back("(no matches)");
            } else {
                const std::size_t first = pickSel_ >= maxLines ? pickSel_ - maxLines + 1 : 0;
                for (std::size_t i = first; i < pickHits_.size() && v.overlay.size() < maxLines; ++i) {
                    v.overlay.push_back(pickHits_[i]);
                    v.overlayMarks.push_back(fuzzyPositions(pickQuery_, pickHits_[i]));
                }
                v.overlaySelected = static_cast<int>(pickSel_ - first);
            }
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

        for (const Placement &p: placed) {
            const std::size_t index = indexOfWindow(p.id);
            WindowView w;
            w.rect = p.rect;
            w.active = index == activeWin_;
            w.number = number_;
            w.relative = relative_;
            w.wrap = wrap_;
            w.bidi = bidi_;
            const int rows = std::max(1, p.rect.rows - 1);
            const bool numbers = number_ || relative_;

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

                const int g = gutterWidth(numbers, buffer_->lineCount(), p.rect.cols);
                const ViewPlace s = placeInView(*buffer_, row_, col_, rowOff_, colOff_, rows,
                                                std::max(1, p.rect.cols - g), wrap_, bidi_);
                w.rowOffset = s.rowOff;
                w.colOffset = s.colOff;
                w.line = row_;
                w.col = static_cast<std::size_t>(renderColumn());
                w.cursorRow = s.cursorRow;
                w.cursorCol = s.cursorCol + g;
            } else {
                const Window &win = windows_[index];
                const std::size_t doc = std::min(win.doc, docs_.size() - 1);
                w.buffer = doc == current_ ? buffer_.get() : docs_[doc].buffer.get();
                w.syntax = doc == current_ ? &syntax_ : nullptr;
                w.fileName = title(doc);
                w.modified = docModified(doc);

                const TextBuffer &text = *w.buffer;
                const std::size_t r = std::min(win.row, text.lineCount() - 1);
                const std::string &line = text.line(r);
                const std::size_t limit = line.empty() ? 0 : prevCharStart(line, line.size());
                const std::size_t c = snapToStart(line, std::min(win.col, limit));

                const int g = gutterWidth(numbers, text.lineCount(), p.rect.cols);
                const ViewPlace s = placeInView(text, r, c, win.rowOff, win.colOff, rows,
                                                std::max(1, p.rect.cols - g), wrap_, bidi_);
                w.rowOffset = s.rowOff;
                w.colOffset = s.colOff;
                w.line = r;
                w.col = static_cast<std::size_t>(displayColumn(line, c));
                w.cursorRow = s.cursorRow;
                w.cursorCol = s.cursorCol + g;
            }
            v.windows.push_back(std::move(w));
        }
        return v;
    }

    void Editor::handleNormalKey(const Key &key) {
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

        if (pendingRegister_) {
            pendingRegister_ = false;
            if (key.type == KeyType::Char && key.ch == '"') {
                selReg_ = 0;
            } else if (key.type == KeyType::Char &&
                       (std::isalnum(static_cast<unsigned char>(key.ch)) != 0 ||
                        key.ch == '_' || key.ch == '+' || key.ch == '*' || key.ch == '-')) {
                selReg_ = key.ch;
            } else {
                resetPending();
            }
            return;
        }

        if (pendingMacro_ != 0) {
            const char kind = pendingMacro_;
            pendingMacro_ = 0;
            if (key.type != KeyType::Char) {
                resetPending();
                return;
            }
            if (kind == 'q') {
                startRecording(key.ch);
            } else {
                playMacro(key.ch, macroCount_);
            }
            return;
        }

        if (pendingMark_ != 0) {
            const char kind = pendingMark_;
            pendingMark_ = 0;
            if (key.type != KeyType::Char) {
                resetPending();
                return;
            }
            if (kind == 'm') {
                takeCount();
                setMark(key.ch);
                return;
            }

            const std::optional<Pos> target = markPos(key.ch);
            if (!target) {
                message_ = "Mark not set";
                resetPending();
                return;
            }

            const bool lines = kind == '\'';
            const char op = pendingOp_;
            takeCount();
            if (op != 0) {
                operate(op, Motion{*target, lines, false});
                return;
            }
            pushJump();
            if (lines) {
                placeCursor({target->row, firstNonBlank(*buffer_, {target->row, 0}).target.col});
            } else {
                placeCursor(*target);
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
                case '"':
                    pendingRegister_ = true;
                    return;
                case 'm':
                case '`':
                case '\'':
                    pendingMark_ = key.ch;
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
            case KeyType::Up: moveUp(un);
                return;
            case KeyType::Down: moveDown(un);
                return;
            case KeyType::Left: moveLeft(un);
                return;
            case KeyType::Right: moveRight(un);
                return;
            case KeyType::Home: moveToLineStart();
                return;
            case KeyType::End: moveToLineEnd();
                return;
            case KeyType::PageUp: pageUp();
                return;
            case KeyType::PageDown: pageDown();
                return;
            case KeyType::Delete: deleteUnderCursor(n);
                return;
            case KeyType::Tab: jumpNewer(n);
                return;
            case KeyType::Ctrl:
                if (key.ch == 'r') {
                    redo(n);
                } else if (key.ch == 'v') {
                    enterVisual('B');
                } else if (key.ch == 'w') {
                    pendingWindow_ = true;
                } else if (key.ch == 'o') {
                    jumpOlder(n);
                }
                return;
            case KeyType::Char: break;
            default: return;
        }

        switch (key.ch) {
            case 'h': moveLeft(un);
                break;
            case 'j': moveDown(un);
                break;
            case 'k': moveUp(un);
                break;
            case 'l': moveRight(un);
                break;
            case '0': moveToLineStart();
                break;
            case '$': moveToLineEnd();
                break;
            case '^': jumpTo(firstNonBlank(*buffer_, here));
                break;
            case 'w': jumpTo(wordForward(*buffer_, here, n));
                break;
            case 'b': jumpTo(wordBackward(*buffer_, here, n));
                break;
            case 'e': jumpTo(wordEnd(*buffer_, here, n));
                break;
            case '}':
                pushJump();
                jumpTo(paragraphForward(*buffer_, here, n));
                break;
            case '{':
                pushJump();
                jumpTo(paragraphBackward(*buffer_, here, n));
                break;
            case '%':
                if (const auto m = matchBracket(*buffer_, here)) {
                    pushJump();
                    jumpTo(*m);
                }
                break;
            case '*': searchWord(true, n);
                break;
            case '#': searchWord(false, n);
                break;
            case ';': repeatFind(false, n);
                break;
            case ',': repeatFind(true, n);
                break;
            case '/': beginSearch(true);
                break;
            case '?': beginSearch(false);
                break;
            case 'n':
                pushJump();
                findNext(false, n);
                break;
            case 'N':
                pushJump();
                findNext(true, n);
                break;
            case 'G':
                pushJump();
                moveToLine(counted ? un - 1 : buffer_->lineCount() - 1);
                break;
            case 'g':
                pushJump();
                moveToLine(counted ? un - 1 : 0);
                break;
            case 'D': operate('d', lineEnd(*buffer_, here));
                break;
            case 'C': operate('c', lineEnd(*buffer_, here));
                break;
            case 'Y': operate('y', gotoLine(*buffer_, row_ + un - 1));
                break;
            case 'S': operate('c', gotoLine(*buffer_, row_ + un - 1));
                break;
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
            case 'x': deleteUnderCursor(n);
                break;
            case 'p': put(true, n);
                break;
            case 'P': put(false, n);
                break;
            case 'u': undo(n);
                break;
            case 'q':
                if (recording_ != 0) {
                    stopRecording();
                } else {
                    pendingMacro_ = 'q';
                }
                break;
            case '@':
                pendingMacro_ = '@';
                macroCount_ = n;
                break;
            case '.': repeatChange(n);
                break;
            case 'J': joinLines(n);
                break;
            case '~': toggleCase(n);
                break;
            case 'r':
                pendingReplace_ = true;
                replaceCount_ = n;
                break;
            case 'v': enterVisual('v');
                break;
            case 'V': enterVisual('V');
                break;
            case 'i':
                enterInsert();
                break;
            case 'a': {
                const std::string &line = buffer_->line(row_);
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
            case 'o': openLine(true);
                break;
            case 'O': openLine(false);
                break;
            case ':':
                mode_ = Mode::Command;
                commandText_.clear();
                resetPrompt(0);
                break;
            default: break;
        }
    }

    void Editor::handleOperatorKey(const Key &key, bool wasPendingG) {
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
                case '`':
                case '\'':
                    pendingMark_ = key.ch;
                    return;
                case '/':
                case '?': {
                    const int count = takeCount();
                    beginSearch(key.ch == '/');
                    searchOp_ = op;
                    searchCount_ = count;
                    return;
                }
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

    void Editor::handleVisualKey(const Key &key) {
        if (pendingFind_ != 0 || pendingMark_ != 0 || pendingRegister_) {
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
            case '%':
            case '"':
            case '`':
            case '\'':
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
            case 'x': operateVisual('d', false);
                return;
            case 'D':
            case 'X': operateVisual('d', true);
                return;
            case 'y': operateVisual('y', false);
                return;
            case 'Y': operateVisual('y', true);
                return;
            case 'c':
            case 's': operateVisual('c', false);
                return;
            case 'C':
            case 'S':
            case 'R': operateVisual('c', true);
                return;
            case 'p':
            case 'P': visualPut();
                return;
            case '>': operateVisual('>', true);
                return;
            case '<': operateVisual('<', true);
                return;
            case ':': {
                const std::size_t first = std::min(anchor_.row, row_);
                const std::size_t last = std::max(anchor_.row, row_);
                leaveVisual();
                mode_ = Mode::Command;
                commandText_ = std::to_string(first + 1) + "," + std::to_string(last + 1);
                resetPrompt(commandText_.size());
                return;
            }
            default: return;
        }
    }

    void Editor::handleInsertKey(const Key &key) {
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
            case KeyType::Escape: leaveInsert();
                break;
            case KeyType::Char: insertString(std::string(1, key.ch));
                break;
            case KeyType::Text: insertString(key.text);
                break;
            case KeyType::Tab: insertTab();
                break;
            case KeyType::Enter: insertNewline();
                break;
            case KeyType::Backspace: backspace();
                break;
            case KeyType::Delete: deleteForward();
                break;
            case KeyType::Up: moveUp();
                break;
            case KeyType::Down: moveDown();
                break;
            case KeyType::Left: moveLeft();
                break;
            case KeyType::Right: moveRight();
                break;
            case KeyType::Home: moveToLineStart();
                break;
            case KeyType::End: moveToLineEnd();
                break;
            case KeyType::PageUp: pageUp();
                break;
            case KeyType::PageDown: pageDown();
                break;
            default: break;
        }
    }

    void Editor::handleCommandKey(const Key &key) {
        if (key.type == KeyType::Escape || (key.type == KeyType::Ctrl && key.ch == 'c')) {
            commandText_.clear();
            mode_ = Mode::Normal;
            return;
        }

        if (key.type == KeyType::Backspace && commandText_.empty()) {
            mode_ = Mode::Normal;
            return;
        }

        if (key.type == KeyType::Enter) {
            const std::string text = commandText_;
            remember(cmdHist_, text);
            commandText_.clear();
            mode_ = Mode::Normal;
            runCommand(text);
            return;
        }

        promptKey(key);
    }

    void Editor::handleSearchKey(const Key &key) {
        if (key.type == KeyType::Escape || (key.type == KeyType::Ctrl && key.ch == 'c')) {
            cancelSearch();
            return;
        }

        if (key.type == KeyType::Backspace && searchText_.empty()) {
            cancelSearch();
            return;
        }

        if (key.type == KeyType::Enter) {
            commitSearch();
            return;
        }

        if (promptKey(key)) previewSearch();
    }

    void Editor::runCommand(const std::string &text) {
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

        std::size_t at = 0;
        std::size_t first = row_;
        std::size_t last = row_;
        bool ranged = false;
        const std::size_t total = buffer_->lineCount();

        if (line[0] == '%') {
            first = 0;
            last = total - 1;
            at = 1;
            ranged = true;
        } else if (const auto a = parseAddress(line, at, row_, total)) {
            first = *a;
            last = *a;
            ranged = true;
            if (at < line.size() && line[at] == ',') {
                ++at;
                const auto b = parseAddress(line, at, row_, total);
                if (!b) {
                    message_ = "Invalid range";
                    return;
                }
                last = *b;
            }
        }
        if (first > last) std::swap(first, last);

        const std::string rest = trim(line.substr(at));
        if (rest.empty()) {
            if (ranged) {
                pushJump();
                placeCursor({last, firstNonBlank(*buffer_, {last, 0}).target.col});
            }
            return;
        }

        if ((rest[0] == 'g' || rest[0] == 'v') && rest.size() > 1) {
            const bool bang = rest[0] == 'g' && rest[1] == '!';
            const std::size_t d = bang ? 2 : 1;
            if (d < rest.size() && std::isalnum(static_cast<unsigned char>(rest[d])) == 0 &&
                rest[d] != ' ' && rest[d] != '\\') {
                const char delim = rest[d];
                std::string pattern;
                std::size_t i = d + 1;
                while (i < rest.size() && rest[i] != delim) {
                    if (rest[i] == '\\' && i + 1 < rest.size() && rest[i + 1] == delim) {
                        pattern += delim;
                        i += 2;
                    } else {
                        pattern += rest[i++];
                    }
                }
                if (i < rest.size()) ++i;
                const std::string command = trim(rest.substr(i));
                if (!ranged) {
                    first = 0;
                    last = total - 1;
                }
                globalCommand(first, last, pattern, rest[0] == 'v' || bang, command);
                return;
            }
        }

        const auto split = rest.find_first_of(" \t");
        std::string name = rest.substr(0, split);
        std::string argument = split == std::string::npos ? "" : trim(rest.substr(split));

        if (name.size() > 1 && (name[0] == 'm' || name[0] == 't') &&
            (std::isdigit(static_cast<unsigned char>(name[1])) != 0 || name[1] == '$' ||
             name[1] == '.')) {
            argument = name.substr(1);
            name = name.substr(0, 1);
        }

        bool force = false;
        if (name.size() > 1 && name.back() == '!') {
            force = true;
            name.pop_back();
        }

        if (const auto alias = aliases_.find(name); alias != aliases_.end()) {
            name = alias->second;
        } else {
            name = builtinAlias(name);
        }

        if (runRangeCommand(name, argument, first, last)) return;

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
        } else if (name == "reg" || name == "registers" || name == "di" || name == "display") {
            showRegisters();
        } else if (name == "set" || name == "se") {
            if (argument.empty()) {
                message_ = optionSummary();
            } else {
                std::istringstream words(argument);
                std::string word;
                while (words >> word) {
                    if (!setOption(word)) {
                        message_ = "Unknown option: " + word;
                        break;
                    }
                }
            }
        } else if (name == "map") {
            mapCommand(argument, false);
        } else if (name == "unmap") {
            mapCommand(argument, true);
        } else if (name == "r" || name == "read") {
            readFile(argument, last);
        } else if (name == "noh" || name == "nohlsearch") {
            highlight_ = false;
        } else if (name == "syntax") {
            if (argument == "on") {
                syntax_.setEnabled(true);
            } else if (argument == "off") {
                syntax_.setEnabled(false);
            } else if (argument.empty()) {
                message_ = syntax_.enabled()
                               ? "syntax on (" + syntax_.languageName() + ")"
                               : "syntax off";
            } else {
                message_ = "usage: :syntax on|off";
            }
        } else if (name == "find" || name == "fin") {
            findCommand(argument);
        } else if (name == "fb") {
            findBufferCommand(argument);
        } else if (name == "pick") {
            openPicker(false);
        } else if (name == "pickb") {
            openPicker(true);
        } else if (name == "help" || name == "h") {
            showHelp(argument);
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

    bool Editor::save(const std::string &argument) {
        const std::string target = argument.empty() ? path_ : argument;
        if (target.empty()) {
            message_ = "No file name";
            return false;
        }

        const std::optional<std::size_t> bytes = saveFile(target, *buffer_, crlf_);
        if (!bytes) {
            message_ = "Cannot write \"" + target + "\"";
            return false;
        }

        if (path_.empty()) {
            path_ = target;
            refreshSyntax();
        }
        if (target == path_) history_.markSaved();
        if (target == path_) writeUndo();
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
        Doc &doc = docs_[current_];
        doc.buffer = std::move(buffer_);
        doc.history = std::move(history_);
        doc.path = path_;
        doc.row = row_;
        doc.col = col_;
        doc.wantedCol = wantedCol_;
        doc.rowOff = rowOff_;
        doc.colOff = colOff_;
        doc.marks = std::move(marks_);
        doc.jumps = std::move(jumps_);
        doc.jumpIdx = jumpIdx_;
        doc.filetype = std::move(filetype_);
        doc.crlf = crlf_;
        filetype_.clear();
        marks_.clear();
        jumps_.clear();
    }

    void Editor::activate(std::size_t index) {
        Doc &doc = docs_[index];
        buffer_ = std::move(doc.buffer);
        history_ = std::move(doc.history);
        path_ = doc.path;
        row_ = doc.row;
        col_ = doc.col;
        wantedCol_ = doc.wantedCol;
        rowOff_ = doc.rowOff;
        colOff_ = doc.colOff;
        marks_ = std::move(doc.marks);
        jumps_ = std::move(doc.jumps);
        jumpIdx_ = doc.jumpIdx;
        filetype_ = doc.filetype;
        crlf_ = doc.crlf;
        current_ = index;

        mode_ = Mode::Normal;
        blockInsert_.active = false;
        cmdKeys_.clear();
        resetPending();
        refreshSyntax();
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

    void Editor::edit(const std::string &path) {
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
        if (!path_.empty()) restorePosition();
        if (!path_.empty()) restoreUndo();

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
            marks_.clear();
            jumps_.clear();
            jumpIdx_ = 0;
            path_.clear();
            row_ = 0;
            col_ = 0;
            wantedCol_ = 0;
            rowOff_ = 0;
            colOff_ = 0;
            mode_ = Mode::Normal;
            resetPending();
            filetype_.clear();
            crlf_ = false;
            refreshSyntax();
            message_ = "Buffer closed";
            return;
        }

        const std::size_t gone = current_;
        const std::size_t next = current_ + 1 < docs_.size() ? current_ : current_ - 1;
        docs_.erase(docs_.begin() + static_cast<std::ptrdiff_t>(current_));
        for (Window &w: windows_) {
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

    void Editor::gotoBuffer(const std::string &argument) {
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

            Doc &doc = docs_[i];
            if (!doc.history.modified()) continue;
            if (doc.path.empty() || !saveFile(doc.path, *doc.buffer, doc.crlf)) {
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
        Window &w = windows_[activeWin_];
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

        const std::size_t next = forward
                                     ? (pos + 1) % order.size()
                                     : (pos + order.size() - 1) % order.size();
        focusWindow(indexOfWindow(order[next]));
    }

    void Editor::splitWindow(bool sideBySide, const std::string &path) {
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

    void Editor::handleWindowKey(const Key &key) {
        takeCount();

        std::optional<Direction> direction;
        switch (key.type) {
            case KeyType::Left: direction = Direction::Left;
                break;
            case KeyType::Right: direction = Direction::Right;
                break;
            case KeyType::Up: direction = Direction::Up;
                break;
            case KeyType::Down: direction = Direction::Down;
                break;
            case KeyType::Ctrl:
                if (key.ch == 'w') cycleWindow(true);
                return;
            case KeyType::Char: break;
            default: return;
        }

        if (!direction) {
            switch (key.ch) {
                case 'h': direction = Direction::Left;
                    break;
                case 'j': direction = Direction::Down;
                    break;
                case 'k': direction = Direction::Up;
                    break;
                case 'l': direction = Direction::Right;
                    break;
                case 'w': cycleWindow(true);
                    return;
                case 'W':
                case 'p': cycleWindow(false);
                    return;
                case 'c':
                case 'q': closeWindow();
                    return;
                case 'o': onlyWindow();
                    return;
                case 's': splitWindow(false, "");
                    return;
                case 'v': splitWindow(true, "");
                    return;
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
            const std::string &line = buffer_->line(p.row);
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
        const Register before = reg_;

        reg_.rows.clear();
        reg_.text.clear();
        std::vector<std::pair<std::size_t, std::size_t> > spans;
        for (std::size_t r = bounds.top; r <= bounds.bottom; ++r) {
            const std::string &line = buffer_->line(r);
            const auto span = byteSpan(line, bounds.left, bounds.right);
            spans.push_back(span);
            reg_.rows.push_back(line.substr(span.first, span.second - span.first));
            if (r > bounds.top) reg_.text += '\n';
            reg_.text += reg_.rows.back();
        }
        reg_.linewise = false;
        reg_.block = true;
        storeRegisters(op == 'y', before);

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
        const std::string &line = buffer_->line(block.top);
        if (line.size() <= block.oldLen || col_ < block.col) return;

        const std::string typed = line.substr(block.col, line.size() - block.oldLen);
        const std::size_t last = std::min(block.bottom, buffer_->lineCount() - 1);
        for (std::size_t r = block.top + 1; r <= last; ++r) {
            const std::string &other = buffer_->line(r);
            if (static_cast<std::size_t>(displayWidth(other)) < block.dcol) continue;
            const std::size_t at = byteForColumn(other, block.dcol);
            applyEdit({r, at}, {r, at}, typed);
        }
    }

    void Editor::visualPut() {
        const int n = takeCount();

        const char sel = selReg_;
        selReg_ = 0;
        if (sel != 0) {
            const std::optional<Register> source = fetchRegister(sel);
            if (!source || source->text.empty()) {
                message_ = std::string("Register ") + sel + " is empty";
                leaveVisual();
                return;
            }
            reg_ = *source;
        }

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
        const bool atEnd = linewise
                               ? b.row + 1 >= buffer_->lineCount()
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
        resetPrompt(0);
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
        searchOp_ = 0;
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
        remember(searchHist_, pattern);

        const char op = searchOp_;
        searchOp_ = 0;
        if (op != 0) {
            searchOperate(op);
            return;
        }

        pushJump();
        findNext(false, 1);
    }

    void Editor::findNext(bool reverse, int count) {
        if (lastSearch_.empty()) {
            message_ = "No previous search pattern";
            return;
        }

        std::string patternError;
        if (!validatePattern(lastSearch_, patternError)) {
            message_ = "Invalid pattern: " + patternError;
            return;
        }

        const bool forward = reverse ? !lastForward_ : lastForward_;
        Pos pos{row_, col_};
        bool wrapped = false;

        for (int i = 0; i < count; ++i) {
            const std::optional<SearchHit> hit = forward
                                                     ? searchForward(*buffer_, pos, lastSearch_)
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
            message_ = forward
                           ? "search hit BOTTOM, continuing at TOP"
                           : "search hit TOP, continuing at BOTTOM";
        } else {
            message_ = std::string(forward ? "/" : "?") + lastSearch_;
        }
    }

    void Editor::substitute(std::size_t first, std::size_t last, std::string pattern,
                            const std::string &replacement, bool global, CaseMode mode) {
        if (pattern.empty()) pattern = lastSearch_;
        if (pattern.empty()) {
            message_ = "No previous search pattern";
            return;
        }

        std::string patternError;
        if (!validatePattern(pattern, patternError)) {
            message_ = "Invalid pattern: " + patternError;
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

    void Editor::insertString(const std::string &text) {
        applyEdit({row_, col_}, {row_, col_}, text);
        col_ += text.size();
        rememberColumn();
    }

    void Editor::insertTab() {
        if (!expandtab_) {
            insertString("\t");
            return;
        }
        const int width = tabStop();
        const int column = renderColumn();
        insertString(std::string(static_cast<std::size_t>(width - column % width), ' '));
    }

    void Editor::insertNewline() {
        std::string indent;
        if (autoindent_) {
            const std::string &line = buffer_->line(row_);
            const std::size_t lead = std::min(line.find_first_not_of(" \t"), line.size());
            indent = line.substr(0, std::min(lead, col_));
        }
        applyEdit({row_, col_}, {row_, col_}, "\n" + indent);
        ++row_;
        col_ = indent.size();
        rememberColumn();
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
        const std::string &line = buffer_->line(row_);
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
        std::string indent;
        if (autoindent_) {
            const std::string &line = buffer_->line(row_);
            const std::size_t lead = std::min(line.find_first_not_of(" \t"), line.size());
            indent = line.substr(0, lead);
        }

        if (below) {
            const std::size_t length = buffer_->line(row_).size();
            applyEdit({row_, length}, {row_, length}, "\n" + indent);
            ++row_;
        } else {
            applyEdit({row_, 0}, {row_, 0}, indent + "\n");
        }
        col_ = indent.size();
        enterInsert();
    }

    void Editor::applyEdit(Pos start, Pos end, const std::string& text) {
        changed_ = true;
        adjustMarks(start, end, text);
        adjustTracked(start, end, text);
        Edit edit = buffer_->replace(start, end, text);
        syntax_.edited(*buffer_, start.row, end.row - start.row,
                       endOf(start, text).row - start.row);
        history_.record(std::move(edit));
    }

    void Editor::undo(int count) {
        std::optional<Pos> where;
        std::size_t low = kEnd;
        for (int i = 0; i < count; ++i) {
            const std::optional<Pos> next = history_.undo(*buffer_);
            if (!next) break;
            where = next;
            low = std::min(low, history_.touchedRow());
        }
        if (low != kEnd) syntax_.invalidate(low);
        if (where) {
            placeCursor(*where);
        } else {
            message_ = "Already at oldest change";
        }
    }

    void Editor::redo(int count) {
        std::optional<Pos> where;
        std::size_t low = kEnd;
        for (int i = 0; i < count; ++i) {
            const std::optional<Pos> next = history_.redo(*buffer_);
            if (!next) break;
            where = next;
            low = std::min(low, history_.touchedRow());
        }
        if (low != kEnd) syntax_.invalidate(low);
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
        pendingRegister_ = false;
        pendingMacro_ = 0;
        pendingMark_ = 0;
        selReg_ = 0;
        pendingG_ = false;
        count_ = 0;
        opCount_ = 0;
    }

    std::string Editor::pendingText() const {
        std::string text;
        if (pendingRegister_) {
            text += '"';
        } else if (selReg_ != 0) {
            text += '"';
            text += selReg_;
        }
        if (opCount_ > 0) text += std::to_string(opCount_);
        if (pendingOp_ != 0) text += pendingOp_;
        if (count_ > 0) text += std::to_string(count_);
        if (pendingObject_ != 0) text += pendingObject_;
        if (pendingFind_ != 0) text += pendingFind_;
        if (pendingReplace_) text += 'r';
        if (pendingMacro_ != 0) text += pendingMacro_;
        if (pendingMark_ != 0) text += pendingMark_;
        if (pendingWindow_) text += "^W";
        if (pendingG_) text += 'g';
        return text;
    }

    std::optional<Motion> Editor::operatorMotion(const Key &key, bool wasPendingG, int count,
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
            case '%': return matchBracket(*buffer_, here);
            case 'G': return gotoLine(*buffer_, counted ? line : buffer_->lineCount() - 1);
            case 'g':
                if (wasPendingG) return gotoLine(*buffer_, counted ? line : 0);
                return std::nullopt;
            default: return std::nullopt;
        }
    }

    void Editor::operate(char op, const Motion &motion) {
        operateRange(op, makeRange(*buffer_, {row_, col_}, motion));
    }

    void Editor::operateRange(char op, const TextRange &range) {
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

        const Register before = reg_;
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
        storeRegisters(op == 'y', before);

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

    std::size_t Editor::shiftWidth() const {
        return shiftwidth_ > 0
                   ? static_cast<std::size_t>(shiftwidth_)
                   : static_cast<std::size_t>(tabStop());
    }

    void Editor::shiftLines(bool right, std::size_t first, std::size_t last) {
        last = std::min(last, buffer_->lineCount() - 1);
        const std::size_t width = shiftWidth();
        const bool useTab = !expandtab_ && width == static_cast<std::size_t>(tabStop());

        for (std::size_t r = first; r <= last; ++r) {
            const std::string &line = buffer_->line(r);
            if (right) {
                if (line.empty()) continue;
                applyEdit({r, 0}, {r, 0}, useTab ? std::string("\t") : std::string(width, ' '));
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
            const std::string &cur = buffer_->line(row_);
            const std::string &next = buffer_->line(row_ + 1);
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
        for (char &c: text) {
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

    void Editor::replaceChars(const std::string &with, int count) {
        const std::string &line = buffer_->line(row_);
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

    void Editor::putFrom(bool after, int count) {
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

            const std::string &current = buffer_->line(row_);
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
        for (const std::string &piece: reg_.rows) {
            width = std::max(width, static_cast<std::size_t>(displayWidth(piece)));
        }

        const std::string &current = buffer_->line(row_);
        const std::size_t length = current.size();
        const std::size_t at =
                after && length > 0 ? std::min(col_ + charLength(current, col_), length) : col_;
        const auto atCol = static_cast<std::size_t>(displayColumn(current, at));
        const std::size_t first = row_;

        for (std::size_t i = 0; i < reg_.rows.size(); ++i) {
            const std::string &source = reg_.rows[i];
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

            const std::string &line = buffer_->line(r);
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

    bool Editor::anyPending() const {
        return pendingOp_ != 0 || pendingFind_ != 0 || pendingObject_ != 0 || pendingReplace_ ||
               pendingWindow_ || pendingRegister_ || pendingMacro_ != 0 || pendingMark_ != 0 ||
               pendingG_ || count_ != 0 || opCount_ != 0;
    }

    void Editor::put(bool after, int count) {
        if (selReg_ == 0) {
            putFrom(after, count);
            return;
        }

        const char name = selReg_;
        selReg_ = 0;

        const std::optional<Register> source = fetchRegister(name);
        if (!source || source->text.empty()) {
            message_ = std::string("Register ") + name + " is empty";
            return;
        }

        const Register saved = reg_;
        reg_ = *source;
        putFrom(after, count);
        reg_ = saved;
    }

    void Editor::storeRegisters(bool yank, const Register &before) {
        const char sel = selReg_;
        selReg_ = 0;

        if (sel == '_') {
            reg_ = before;
            return;
        }

        if (sel == 0 || sel == '"') {
            if (yank) {
                numbered_[0] = reg_;
            } else if (reg_.linewise || reg_.text.find('\n') != std::string::npos) {
                for (std::size_t i = 9; i > 1; --i) numbered_[i] = numbered_[i - 1];
                numbered_[1] = reg_;
            } else {
                small_ = reg_;
            }
            return;
        }

        if (sel == '+' || sel == '*') {
            clip_ = reg_;
            clipboardWrite(reg_.text);
            return;
        }

        if (std::isalpha(static_cast<unsigned char>(sel)) != 0) {
            selReg_ = sel;
            storeNamed();
        }
    }

    std::optional<Editor::Register> Editor::fetchRegister(char name) {
        if (name == '"') return reg_;
        if (name == '_') return std::nullopt;
        if (name == '-') return small_;

        if (name >= '0' && name <= '9') return numbered_[static_cast<std::size_t>(name - '0')];

        if (name == '+' || name == '*') {
            const std::optional<std::string> text = clipboardRead();
            if (!text) {
                if (clip_.text.empty()) return std::nullopt;
                return clip_;
            }
            if (*text == clip_.text) return clip_;

            Register r;
            r.text = *text;
            r.linewise = !text->empty() && text->back() == '\n';
            return r;
        }

        if (std::isalpha(static_cast<unsigned char>(name)) != 0) {
            const auto it = named_.find(lowerChar(name));
            if (it == named_.end()) return std::nullopt;
            return it->second;
        }
        return std::nullopt;
    }

    void Editor::storeNamed() {
        if (selReg_ == 0) return;

        const char name = lowerChar(selReg_);
        const bool append = std::isupper(static_cast<unsigned char>(selReg_)) != 0;
        selReg_ = 0;

        const auto it = named_.find(name);
        if (!append || it == named_.end()) {
            named_[name] = reg_;
            return;
        }

        Register &dest = it->second;
        const bool lines = dest.linewise || reg_.linewise;
        if (lines && !dest.text.empty() && dest.text.back() != '\n') dest.text += '\n';
        dest.text += reg_.text;
        dest.linewise = lines;
        dest.block = false;
        dest.rows.clear();
    }

    void Editor::startRecording(char name) {
        if (std::isalpha(static_cast<unsigned char>(name)) == 0) {
            message_ = "Invalid register name";
            return;
        }
        recording_ = name;
        recordKeys_.clear();
    }

    void Editor::stopRecording() {
        if (!recordKeys_.empty()) recordKeys_.pop_back();

        const char name = lowerChar(recording_);
        const bool append = std::isupper(static_cast<unsigned char>(recording_)) != 0;

        std::vector<Key> &dest = macros_[name];
        if (append) {
            dest.insert(dest.end(), recordKeys_.begin(), recordKeys_.end());
        } else {
            dest = recordKeys_;
        }

        recording_ = 0;
        recordKeys_.clear();
        message_ = std::string("recorded @") + name;
    }

    void Editor::playMacro(char name, int count) {
        if (name == '@') name = lastMacro_;
        if (name == 0) {
            message_ = "No previous macro";
            return;
        }

        const char key = lowerChar(name);
        const auto it = macros_.find(key);
        if (it == macros_.end() || it->second.empty()) {
            message_ = std::string("Register @") + key + " is empty";
            return;
        }
        if (macroDepth_ >= kMaxMacroDepth) {
            message_ = "Macro nested too deeply";
            return;
        }

        lastMacro_ = key;
        const std::vector<Key> keys = it->second;

        if (macroDepth_ == 0) macroBudget_ = kMacroBudget;
        ++macroDepth_;
        cmdKeys_.clear();
        changed_ = false;

        for (int i = 0; i < count && macroBudget_ > 0 && !quit_; ++i) {
            for (const Key &k: keys) {
                if (macroBudget_ <= 0 || quit_) break;
                --macroBudget_;
                dispatch(k);
            }
        }

        --macroDepth_;
        if (macroBudget_ <= 0) message_ = "Macro stopped: too many keys";
    }

    void Editor::showRegisters() {
        std::string text;
        auto add = [&](const std::string &label, const std::string &body) {
            if (!text.empty()) text += "  ";
            text += label + " " + body;
        };

        if (!reg_.text.empty()) add("\"\"", preview(reg_.text));
        for (std::size_t i = 0; i < numbered_.size(); ++i) {
            if (numbered_[i].text.empty()) continue;
            add(std::string("\"") + static_cast<char>('0' + i), preview(numbered_[i].text));
        }
        if (!small_.text.empty()) add("\"-", preview(small_.text));
        for (const auto &[name, content]: named_) {
            if (!content.text.empty()) add(std::string("\"") + name, preview(content.text));
        }
        for (const auto &[name, keys]: macros_) {
            std::string joined;
            for (const Key &k: keys) joined += keyLabel(k);
            if (!joined.empty()) add(std::string("@") + name, preview(joined));
        }

        message_ = text.empty() ? "No registers" : text;
    }

    void Editor::setMark(char name) {
        if (std::isalpha(static_cast<unsigned char>(name)) == 0) {
            message_ = "Invalid mark name";
            return;
        }
        marks_[name] = {row_, col_};
    }

    std::optional<Pos> Editor::markPos(char name) const {
        if (name == '`') name = '\'';
        const auto it = marks_.find(name);
        if (it == marks_.end()) return std::nullopt;

        Pos p = it->second;
        p.row = std::min(p.row, buffer_->lineCount() - 1);
        const std::string &line = buffer_->line(p.row);
        p.col = snapToStart(line, std::min(p.col, line.size()));
        return p;
    }

    void Editor::pushJump() {
        const Pos here{row_, col_};
        marks_['\''] = here;
        jumps_.erase(std::remove_if(jumps_.begin(), jumps_.end(),
                                    [&](const Pos &p) { return p.row == here.row; }),
                     jumps_.end());
        jumps_.push_back(here);
        if (jumps_.size() > 100) jumps_.erase(jumps_.begin());
        jumpIdx_ = jumps_.size();
    }

    void Editor::jumpOlder(int count) {
        if (jumps_.empty()) {
            message_ = "Jump list is empty";
            return;
        }
        if (jumpIdx_ >= jumps_.size()) {
            pushJump();
            jumpIdx_ = jumps_.size() - 1;
        }
        if (jumpIdx_ == 0) {
            message_ = "Already at oldest jump";
            return;
        }
        const auto n = static_cast<std::size_t>(count);
        jumpIdx_ = jumpIdx_ > n ? jumpIdx_ - n : 0;
        placeCursor(jumps_[jumpIdx_]);
    }

    void Editor::jumpNewer(int count) {
        if (jumpIdx_ + 1 >= jumps_.size()) {
            message_ = "Already at newest jump";
            return;
        }
        const auto n = static_cast<std::size_t>(count);
        jumpIdx_ = std::min(jumpIdx_ + n, jumps_.size() - 1);
        placeCursor(jumps_[jumpIdx_]);
    }

    void Editor::adjustMarks(Pos start, Pos end, const std::string &text) {
        if (marks_.empty() && jumps_.empty()) return;

        const Pos newEnd = endOf(start, text);
        auto fix = [&](Pos &p) {
            if (p < start) return;
            if (p < end) {
                p = start;
                return;
            }
            if (p.row == end.row) {
                p = {newEnd.row, newEnd.col + (p.col - end.col)};
            } else {
                p.row = p.row - (end.row - start.row) + (newEnd.row - start.row);
            }
        };

        for (auto &entry: marks_) fix(entry.second);
        for (Pos &p: jumps_) fix(p);
    }

    void Editor::searchWord(bool forward, int count) {
        const std::string &line = buffer_->line(row_);
        auto isWord = [&](std::size_t i) {
            const auto u = static_cast<unsigned char>(line[i]);
            return std::isalnum(u) != 0 || u == '_' || u >= 128;
        };

        std::size_t i = col_;
        while (i < line.size() && !isWord(i)) ++i;
        if (i >= line.size()) {
            message_ = "No word under cursor";
            return;
        }

        std::size_t start = i;
        while (start > 0 && isWord(start - 1)) --start;
        std::size_t stop = i;
        while (stop < line.size() && isWord(stop)) ++stop;

        lastSearch_ = "\\<" + line.substr(start, stop - start) + "\\>";
        lastForward_ = forward;
        highlight_ = true;

        pushJump();
        col_ = start;
        rememberColumn();
        findNext(false, count);
    }

    void Editor::searchOperate(char op) {
        if (lastSearch_.empty()) {
            message_ = "No previous search pattern";
            return;
        }

        Pos pos{row_, col_};
        for (int i = 0; i < searchCount_; ++i) {
            const std::optional<SearchHit> hit = lastForward_
                                                     ? searchForward(*buffer_, pos, lastSearch_)
                                                     : searchBackward(*buffer_, pos, lastSearch_);
            if (!hit) {
                message_ = "Pattern not found: " + lastSearch_;
                return;
            }
            pos = hit->pos;
        }

        highlight_ = true;
        operate(op, Motion{pos, false, false});
    }

    void Editor::adjustTracked(Pos start, Pos end, const std::string &text) {
        if (tracked_.empty()) return;

        const std::size_t removed = end.row - start.row;
        const std::size_t added = endOf(start, text).row - start.row;
        if (removed == 0 && added == 0) return;

        const bool wholeLines = removed > 0 && start.col == 0 && end.col == 0 && text.empty();
        const bool pushDown = removed == 0 && start.col == 0 && !text.empty() && text.back() == '\n';

        for (std::size_t i = trackedFrom_; i < tracked_.size(); ++i) {
            std::size_t &r = tracked_[i];
            if (r == kEnd) continue;
            if (wholeLines) {
                if (r >= end.row) {
                    r -= removed;
                } else if (r >= start.row) {
                    r = kEnd;
                }
            } else if (pushDown) {
                if (r >= start.row) r += added;
            } else if (r > end.row) {
                r = r - removed + added;
            } else if (r > start.row) {
                r = kEnd;
            }
        }
    }

    void Editor::deleteLineRange(std::size_t first, std::size_t last) {
        if (last + 1 < buffer_->lineCount()) {
            applyEdit({first, 0}, {last + 1, 0}, "");
        } else if (first > 0) {
            applyEdit({first - 1, buffer_->line(first - 1).size()},
                      {last, buffer_->line(last).size()}, "");
        } else {
            applyEdit({0, 0}, {last, buffer_->line(last).size()}, "");
        }
    }

    void Editor::insertLinesAfter(std::size_t dest, const std::string &block) {
        if (dest == 0) {
            applyEdit({0, 0}, {0, 0}, block + "\n");
        } else {
            const std::size_t length = buffer_->line(dest - 1).size();
            applyEdit({dest - 1, length}, {dest - 1, length}, "\n" + block);
        }
    }

    void Editor::runNormal(std::size_t first, std::size_t last, const std::string &text) {
        std::string error;
        const std::optional<std::vector<Key> > keys = parseKeys(text, error);
        if (!keys) {
            message_ = "Invalid keys: " + error;
            return;
        }
        if (keys->empty()) return;

        auto runAt = [&](std::size_t r) {
            if (r >= buffer_->lineCount()) return;
            row_ = r;
            col_ = 0;
            mode_ = Mode::Normal;
            resetPending();
            rememberColumn();

            const bool before = replaying_;
            replaying_ = true;
            for (const Key &key: *keys) dispatch(key);
            replaying_ = before;

            if (mode_ == Mode::Insert) {
                leaveInsert();
            } else {
                mode_ = Mode::Normal;
                commandText_.clear();
                searchText_.clear();
                searchOp_ = 0;
            }
            resetPending();
        };

        const bool own = tracked_.empty() && first < last;
        if (own) {
            for (std::size_t r = first; r <= last; ++r) tracked_.push_back(r);
            trackedFrom_ = 0;
            ++batch_;
            for (std::size_t i = 0; i < tracked_.size() && !quit_; ++i) {
                const std::size_t r = tracked_[i];
                trackedFrom_ = i + 1;
                if (r != kEnd) runAt(r);
            }
            --batch_;
            tracked_.clear();
            trackedFrom_ = 0;
        } else {
            for (std::size_t r = first; r <= last && !quit_; ++r) runAt(r);
        }
        placeCursor({row_, col_});
    }

    void Editor::globalCommand(std::size_t first, std::size_t last, std::string pattern,
                               bool invert, const std::string &command) {
        if (batch_ > 0) {
            message_ = "Cannot use :g inside :g";
            return;
        }
        if (pattern.empty()) pattern = lastSearch_;
        if (pattern.empty()) {
            message_ = "No previous search pattern";
            return;
        }

        std::string patternError;
        if (!validatePattern(pattern, patternError)) {
            message_ = "Invalid pattern: " + patternError;
            return;
        }

        last = std::min(last, buffer_->lineCount() - 1);
        first = std::min(first, last);

        std::vector<std::size_t> rows;
        for (std::size_t r = first; r <= last; ++r) {
            const bool hit = !findInLine(buffer_->line(r), pattern).empty();
            if (hit != invert) rows.push_back(r);
        }

        lastSearch_ = pattern;
        highlight_ = true;

        if (rows.empty()) {
            message_ = (invert ? "No lines without pattern: " : "Pattern not found: ") + pattern;
            return;
        }
        if (command.empty()) {
            message_ = std::to_string(rows.size()) + (rows.size() == 1
                                                          ? " matching line"
                                                          : " matching lines");
            return;
        }

        tracked_ = rows;
        trackedFrom_ = 0;
        ++batch_;
        std::size_t ran = 0;
        for (std::size_t i = 0; i < tracked_.size() && !quit_; ++i) {
            const std::size_t r = tracked_[i];
            trackedFrom_ = i + 1;
            if (r == kEnd || r >= buffer_->lineCount()) continue;
            row_ = r;
            col_ = 0;
            mode_ = Mode::Normal;
            rememberColumn();
            runCommand(command);
            ++ran;
        }
        --batch_;
        tracked_.clear();
        trackedFrom_ = 0;
        placeCursor({row_, col_});

        if (message_.rfind("Not an editor command", 0) != 0) {
            message_ = std::to_string(ran) + (ran == 1 ? " line" : " lines") + " processed";
        }
    }

    bool Editor::runRangeCommand(const std::string &name, const std::string &argument,
                                 std::size_t first, std::size_t last) {
        const bool del = name == "d" || name == "delete";
        const bool yank = name == "y" || name == "yank";
        const bool move = name == "m" || name == "move";
        const bool copy = name == "t" || name == "co" || name == "copy";
        const bool right = name == ">";
        const bool left = name == "<";
        const bool normal = name == "normal" || name == "norm";
        if (!del && !yank && !move && !copy && !right && !left && !normal) return false;

        last = std::min(last, buffer_->lineCount() - 1);
        first = std::min(first, last);

        if (del) {
            operateRange('d', TextRange{{first, 0}, {last, 0}, true});
            return true;
        }
        if (yank) {
            const Pos keep{row_, col_};
            operateRange('y', TextRange{{first, 0}, {last, 0}, true});
            placeCursor(keep);
            return true;
        }
        if (right || left) {
            shiftLines(right, first, last);
            return true;
        }
        if (normal) {
            runNormal(first, last, argument);
            return true;
        }

        std::optional<std::size_t> dest;
        if (argument == ".") {
            dest = row_ + 1;
        } else if (argument == "$") {
            dest = buffer_->lineCount();
        } else if (!argument.empty() && argument.size() <= 6 &&
                   std::all_of(argument.begin(), argument.end(), [](char c) {
                       return std::isdigit(static_cast<unsigned char>(c)) != 0;
                   })) {
            dest = std::min<std::size_t>(std::stoul(argument), buffer_->lineCount());
        }
        if (!dest) {
            message_ = "Invalid address";
            return true;
        }

        const std::size_t d = *dest;
        const std::size_t count = last - first + 1;
        const std::string block = buffer_->text({first, 0}, {last, buffer_->line(last).size()});
        std::size_t newLast = 0;

        if (copy) {
            insertLinesAfter(d, block);
            newLast = d + count - 1;
        } else {
            const std::size_t f = first + 1;
            const std::size_t l = last + 1;
            if (d + 1 >= f && d <= l) {
                if (d >= f && d < l) message_ = "Cannot move a range of lines into itself";
                return true;
            }
            if (d > l) {
                insertLinesAfter(d, block);
                deleteLineRange(first, last);
                newLast = d - 1;
            } else {
                deleteLineRange(first, last);
                insertLinesAfter(d, block);
                newLast = d + count - 1;
            }
        }

        placeCursor({newLast, firstNonBlank(*buffer_, {newLast, 0}).target.col});
        return true;
    }

    std::string &Editor::promptText() {
        return mode_ == Mode::Search ? searchText_ : commandText_;
    }

    void Editor::resetPrompt(std::size_t pos) {
        promptPos_ = pos;
        histIdx_ = kEnd;
        histDraft_.clear();
        complActive_ = false;
        promptReg_ = false;
    }

    void Editor::insertPrompt(const std::string &text) {
        std::string &line = promptText();
        line.insert(promptPos_, text);
        promptPos_ += text.size();
    }

    bool Editor::promptKey(const Key &key) {
        std::string &text = promptText();
        promptPos_ = snapToStart(text, std::min(promptPos_, text.size()));

        if (promptReg_) {
            promptReg_ = false;
            complActive_ = false;

            std::string insert;
            if (key.type == KeyType::Ctrl && key.ch == 'w') {
                const Pos at = mode_ == Mode::Search ? searchOrigin_ : Pos{row_, col_};
                const std::string &line = buffer_->line(at.row);
                auto isWord = [&](std::size_t i) {
                    const auto u = static_cast<unsigned char>(line[i]);
                    return std::isalnum(u) != 0 || u == '_' || u >= 128;
                };
                std::size_t i = at.col;
                while (i < line.size() && !isWord(i)) ++i;
                if (i < line.size()) {
                    std::size_t start = i;
                    while (start > 0 && isWord(start - 1)) --start;
                    std::size_t stop = i;
                    while (stop < line.size() && isWord(stop)) ++stop;
                    insert = line.substr(start, stop - start);
                }
            } else if (key.type == KeyType::Char) {
                if (key.ch == '/') {
                    insert = lastSearch_;
                } else if (const std::optional<Register> r = fetchRegister(key.ch)) {
                    insert = r->text;
                }
            }

            if (!insert.empty() && insert.back() == '\n') insert.pop_back();
            std::replace(insert.begin(), insert.end(), '\n', ' ');
            insertPrompt(insert);
            return true;
        }

        if (key.type != KeyType::Tab) complActive_ = false;
        if (key.type != KeyType::Up && key.type != KeyType::Down) histIdx_ = kEnd;

        switch (key.type) {
            case KeyType::Left:
                if (promptPos_ > 0) promptPos_ = prevCharStart(text, promptPos_);
                return true;
            case KeyType::Right:
                if (promptPos_ < text.size()) promptPos_ += charLength(text, promptPos_);
                return true;
            case KeyType::Home:
                promptPos_ = 0;
                return true;
            case KeyType::End:
                promptPos_ = text.size();
                return true;
            case KeyType::Delete:
                if (promptPos_ < text.size()) text.erase(promptPos_, charLength(text, promptPos_));
                return true;
            case KeyType::Backspace:
                if (promptPos_ > 0) {
                    const std::size_t previous = prevCharStart(text, promptPos_);
                    text.erase(previous, promptPos_ - previous);
                    promptPos_ = previous;
                }
                return true;
            case KeyType::Up:
                historyStep(true);
                return true;
            case KeyType::Down:
                historyStep(false);
                return true;
            case KeyType::Tab:
                if (mode_ == Mode::Command) {
                    completeCommand();
                } else {
                    insertPrompt("\t");
                }
                return true;
            case KeyType::Char:
                insertPrompt(std::string(1, key.ch));
                return true;
            case KeyType::Text:
                insertPrompt(key.text);
                return true;
            case KeyType::Ctrl:
                switch (key.ch) {
                    case 'b': promptPos_ = 0;
                        return true;
                    case 'e': promptPos_ = text.size();
                        return true;
                    case 'u':
                        text.erase(0, promptPos_);
                        promptPos_ = 0;
                        return true;
                    case 'w': {
                        std::size_t start = promptPos_;
                        while (start > 0 && text[start - 1] == ' ') --start;
                        while (start > 0 && text[start - 1] != ' ') start = prevCharStart(text, start);
                        text.erase(start, promptPos_ - start);
                        promptPos_ = start;
                        return true;
                    }
                    case 'r':
                        promptReg_ = true;
                        return true;
                    default: return false;
                }
            default: return false;
        }
    }

    void Editor::historyStep(bool older) {
        std::vector<std::string> &hist = mode_ == Mode::Search ? searchHist_ : cmdHist_;
        std::string &text = promptText();
        if (hist.empty()) return;

        if (histIdx_ == kEnd) {
            if (!older) return;
            histDraft_ = text;
        }

        auto matches = [&](std::size_t k) {
            return hist[k].compare(0, histDraft_.size(), histDraft_) == 0;
        };

        std::size_t i = histIdx_ == kEnd ? hist.size() : histIdx_;
        if (older) {
            while (i > 0) {
                --i;
                if (matches(i)) {
                    histIdx_ = i;
                    text = hist[i];
                    promptPos_ = text.size();
                    return;
                }
            }
            return;
        }

        while (++i < hist.size()) {
            if (matches(i)) {
                histIdx_ = i;
                text = hist[i];
                promptPos_ = text.size();
                return;
            }
        }
        text = histDraft_;
        histIdx_ = kEnd;
        promptPos_ = text.size();
    }

    void Editor::completeCommand() {
        std::string &text = commandText_;

        if (!complActive_) {
            const std::string before = text.substr(0, promptPos_);
            std::size_t lead = 0;
            while (lead < before.size() &&
                   std::string("0123456789,.$%").find(before[lead]) != std::string::npos) {
                ++lead;
            }

            const std::size_t space = before.find_last_of(' ');
            const bool command = space == std::string::npos;
            const std::size_t start = command ? lead : space + 1;
            const std::string base = before.substr(start);

            std::vector<std::string> cands;
            if (command) {
                static const char *const names[] = {
                    "b", "bd", "bdelete", "bn", "bnext", "bp",
                    "bprev", "bprevious", "buffer", "buffers", "close", "copy",
                    "delete", "di", "display", "e", "edit", "khalas",
                    "ls", "map", "move", "noh", "nohlsearch", "normal",
                    "only", "q", "qa", "qall", "quit", "r",
                    "read", "reg", "registers", "reload", "se", "set",
                    "sp", "split", "syntax", "unmap", "vs", "vsp",
                    "vsplit", "w", "wa", "wall", "wq", "wqa",
                    "write", "x", "xa", "yank"
                };
                for (const char *n: names) cands.emplace_back(n);
                for (const auto &entry: aliases_) cands.push_back(entry.first);
                cands.erase(std::remove_if(cands.begin(), cands.end(),
                                           [&](const std::string &c) {
                                               return c.compare(0, base.size(), base) != 0;
                                           }),
                            cands.end());
                std::sort(cands.begin(), cands.end());
                cands.erase(std::unique(cands.begin(), cands.end()), cands.end());
            } else {
                const std::size_t firstSpace = before.find(' ', lead);
                const std::string word = before.substr(lead, firstSpace - lead);
                if (!takesFile(word)) return;
                cands = fileCandidates(base);
            }
            if (cands.empty()) return;

            complStart_ = start;
            complBase_ = base;
            complTail_ = text.substr(promptPos_);

            if (cands.size() == 1) {
                const std::string pick = cands[0] + (command ? " " : "");
                text = text.substr(0, start) + pick + complTail_;
                promptPos_ = start + pick.size();
                return;
            }

            const std::string common = commonPrefix(cands);
            if (common.size() > base.size()) {
                text = text.substr(0, start) + common + complTail_;
                promptPos_ = start + common.size();
                return;
            }

            complCands_ = cands;
            complIdx_ = 0;
            complActive_ = true;
        } else {
            complIdx_ = (complIdx_ + 1) % (complCands_.size() + 1);
        }

        const std::string pick =
                complIdx_ == complCands_.size() ? complBase_ : complCands_[complIdx_];
        text = text.substr(0, complStart_) + pick + complTail_;
        promptPos_ = complStart_ + pick.size();
    }

    void Editor::readFile(const std::string &path, std::size_t after) {
        if (path.empty()) {
            message_ = "usage: :r FILE";
            return;
        }

        LineBuffer incoming;
        const LoadResult result = loadFile(path, incoming);
        if (result.status != LoadStatus::Loaded) {
            message_ = "Can't read \"" + path + "\"";
            return;
        }
        if (result.bytes == 0) {
            message_ = "\"" + path + "\" is empty";
            return;
        }

        const std::size_t count = incoming.lineCount();
        const std::string block = incoming.text({0, 0}, {count - 1, incoming.line(count - 1).size()});

        after = std::min(after, buffer_->lineCount() - 1);
        insertLinesAfter(after + 1, block);
        placeCursor({after + 1, firstNonBlank(*buffer_, {after + 1, 0}).target.col});
        message_ = std::to_string(count) + (count == 1 ? " line" : " lines") + " read from \"" +
                   path + "\"";
    }

    void Editor::findCommand(const std::string &query) {
        if (query.empty()) {
            message_ = "usage: :find QUERY";
            return;
        }

        const std::vector<std::string> files = listFiles(".", 20000);
        const std::vector<std::string> hits = rankFuzzy(query, files, 5);
        if (hits.empty()) {
            message_ = "No file matches: " + query;
            return;
        }

        const std::string best = hits.front();
        edit(best);

        if (hits.size() > 1 && path_ == best) {
            std::string more = " (also:";
            for (std::size_t i = 1; i < hits.size(); ++i) more += " " + hits[i];
            message_ += more + ")";
        }
    }

    void Editor::findBufferCommand(const std::string &query) {
        if (query.empty()) {
            message_ = "usage: :fb QUERY";
            return;
        }

        std::vector<std::string> names;
        std::vector<std::size_t> owners;
        for (std::size_t i = 0; i < docs_.size(); ++i) {
            const std::string name = docPath(i);
            if (name.empty()) continue;
            names.push_back(name);
            owners.push_back(i);
        }

        const std::vector<std::string> hits = rankFuzzy(query, names, 1);
        if (hits.empty()) {
            message_ = "No buffer matches: " + query;
            return;
        }

        for (std::size_t k = 0; k < names.size(); ++k) {
            if (names[k] == hits.front()) {
                switchBuffer(owners[k]);
                message_ = bufferInfo();
                return;
            }
        }
    }

    void Editor::showHelp(const std::string& topic) {
        const bool pristine = path_.empty() && !history_.modified() &&
                              buffer_->lineCount() == 1 && buffer_->line(0).empty();
        if (!pristine) {
            stash();
            Doc fresh;
            fresh.buffer = std::make_unique<LineBuffer>();
            docs_.push_back(std::move(fresh));
            activate(docs_.size() - 1);
        }

        setText(helpText());
        filetype_ = "markdown";
        refreshSyntax();
        message_ = "Help: /word or :help TOPIC searches, :bd closes";

        if (topic.empty()) return;
        lastSearch_ = topic;
        lastForward_ = true;
        findNext(false, 1);
    }

    void Editor::openPicker(bool buffers) {
        if (batch_ > 0 || replaying_) return;

        pickItems_.clear();
        if (buffers) {
            for (std::size_t i = 0; i < docs_.size(); ++i) {
                const std::string name = docPath(i);
                if (!name.empty()) pickItems_.push_back(name);
            }
            if (pickItems_.empty()) {
                message_ = "No named buffers";
                return;
            }
        } else {
            pickItems_ = listFiles(".", 20000);
        }

        pickBuffers_ = buffers;
        pickQuery_.clear();
        pickSel_ = 0;
        resetPending();
        mode_ = Mode::Picker;
        refreshPicker();
    }

    void Editor::refreshPicker() {
        pickHits_ = rankFuzzy(pickQuery_, pickItems_, 10);
        if (pickHits_.empty()) {
            pickSel_ = 0;
        } else {
            pickSel_ = std::min(pickSel_, pickHits_.size() - 1);
        }
    }

    void Editor::closePicker() {
        mode_ = Mode::Normal;
        pickItems_.clear();
        pickHits_.clear();
        pickQuery_.clear();
        pickSel_ = 0;
    }

    void Editor::acceptPicker() {
        if (pickHits_.empty()) {
            const std::string query = pickQuery_;
            closePicker();
            message_ = "No match: " + query;
            return;
        }

        const std::string choice = pickHits_[pickSel_];
        const bool buffers = pickBuffers_;
        closePicker();

        if (!buffers) {
            edit(choice);
            return;
        }
        for (std::size_t i = 0; i < docs_.size(); ++i) {
            if (docPath(i) == choice) {
                switchBuffer(i);
                message_ = bufferInfo();
                return;
            }
        }
    }

    void Editor::handlePickerKey(const Key &key) {
        auto up = [&] {
            if (pickSel_ > 0) --pickSel_;
        };
        auto down = [&] {
            if (pickSel_ + 1 < pickHits_.size()) ++pickSel_;
        };

        switch (key.type) {
            case KeyType::Escape:
                closePicker();
                break;
            case KeyType::Enter:
                acceptPicker();
                break;
            case KeyType::Up:
                up();
                break;
            case KeyType::Down:
                down();
                break;
            case KeyType::Ctrl:
                if (key.ch == 'c') {
                    closePicker();
                } else if (key.ch == 'p') {
                    up();
                } else if (key.ch == 'n') {
                    down();
                } else if (key.ch == 'u') {
                    pickQuery_.clear();
                    pickSel_ = 0;
                    refreshPicker();
                }
                break;
            case KeyType::Backspace:
                if (pickQuery_.empty()) {
                    closePicker();
                } else {
                    popLastChar(pickQuery_);
                    pickSel_ = 0;
                    refreshPicker();
                }
                break;
            case KeyType::Char:
                pickQuery_ += key.ch;
                pickSel_ = 0;
                refreshPicker();
                break;
            case KeyType::Text:
                pickQuery_ += key.text;
                pickSel_ = 0;
                refreshPicker();
                break;
            default:
                break;
        }
    }

    void Editor::mapCommand(const std::string &argument, bool remove) {
        std::istringstream stream(argument);
        std::vector<std::string> words;
        std::string word;
        while (stream >> word) words.push_back(word);

        const std::size_t expected = remove ? 2 : 3;
        if (words.size() != expected) {
            message_ = remove ? "usage: :unmap MODE KEYS" : "usage: :map MODE KEYS KEYS";
            return;
        }

        std::optional<MapMode> mode;
        if (words[0] == "normal" || words[0] == "n") mode = MapMode::Normal;
        if (words[0] == "insert" || words[0] == "i") mode = MapMode::Insert;
        if (words[0] == "visual" || words[0] == "v") mode = MapMode::Visual;
        if (!mode) {
            message_ = "Unknown mode: " + words[0];
            return;
        }

        std::string error;
        const std::optional<std::vector<Key> > lhs = parseKeys(words[1], error);
        if (!lhs || lhs->empty()) {
            message_ = "Invalid keys" + (error.empty() ? std::string() : ": " + error);
            return;
        }

        if (remove) {
            message_ = keymap_.remove(*mode, *lhs) ? "Mapping removed" : "No such mapping";
            return;
        }

        const std::optional<std::vector<Key> > rhs = parseKeys(words[2], error);
        if (!rhs) {
            message_ = "Invalid keys: " + error;
            return;
        }
        keymap_.add(*mode, *lhs, *rhs);
        message_ = "Mapped " + words[1];
    }

    void Editor::moveLeft(std::size_t n) {
        if (bidi_ && moveVisual(-1, n)) return;
        const std::string &line = buffer_->line(row_);
        for (std::size_t i = 0; i < n && col_ > 0; ++i) col_ = prevCharStart(line, col_);
        rememberColumn();
    }

    void Editor::moveRight(std::size_t n) {
        if (bidi_ && moveVisual(1, n)) return;
        const std::string &line = buffer_->line(row_);
        const std::size_t limit = maxColumn(row_);
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t length = charLength(line, col_);
            if (length == 0 || col_ + length > limit) break;
            col_ += length;
        }
        rememberColumn();
    }

    bool Editor::moveVisual(int direction, std::size_t count) {
        const std::string &line = buffer_->line(row_);
        const std::vector<std::size_t> order = visualCellOrder(line);
        if (order.empty()) return false;

        const auto cell = static_cast<std::size_t>(displayColumn(line, col_));
        std::size_t v = order.size();
        for (std::size_t k = 0; k < order.size(); ++k) {
            if (order[k] == cell) {
                v = k;
                break;
            }
        }
        if (v == order.size()) return false;

        const std::size_t limit = maxColumn(row_);
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t here = byteForColumn(line, order[v]);
            bool moved = false;
            std::size_t w = v;
            while (true) {
                if (direction < 0) {
                    if (w == 0) break;
                    --w;
                } else {
                    if (w + 1 >= order.size()) break;
                    ++w;
                }
                const std::size_t b = byteForColumn(line, order[w]);
                if (b != here && b <= limit) {
                    v = w;
                    moved = true;
                    break;
                }
            }
            if (!moved) break;
        }

        col_ = snapToStart(line, byteForColumn(line, order[v]));
        rememberColumn();
        return true;
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

    void Editor::jumpTo(const Motion &motion) {
        row_ = std::min(motion.target.row, buffer_->lineCount() - 1);
        col_ = std::min(motion.target.col, maxColumn(row_));
        col_ = snapToStart(buffer_->line(row_), col_);
        rememberColumn();
    }

    void Editor::startFind(char kind, const std::string &target) {
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

    void Editor::runFind(const FindState &find, bool repeat, int count) {
        Pos start{row_, col_};
        if (repeat && find.till) {
            const std::string &line = buffer_->line(row_);
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
        const std::string &line = buffer_->line(row_);
        if (wantedCol_ == kEnd) {
            col_ = maxColumn(row_);
        } else {
            col_ = std::min(byteForColumn(line, wantedCol_), maxColumn(row_));
        }
        col_ = snapToStart(line, col_);
    }

    void Editor::scroll() {
        const ViewPlace s = placeInView(*buffer_, row_, col_, rowOff_, colOff_,
                                        static_cast<int>(textRows()), textWidth(), wrap_, bidi_);
        rowOff_ = s.rowOff;
        colOff_ = s.colOff;
    }

    int Editor::gutter() const {
        return gutterWidth(number_ || relative_, buffer_->lineCount(), activeRect().cols);
    }

    int Editor::textWidth() const {
        return std::max(1, activeRect().cols - gutter());
    }

    bool Editor::setOption(const std::string &raw) {
        const std::size_t eq = raw.find('=');
        if (eq != std::string::npos) {
            const std::string key = raw.substr(0, eq);
            const std::string value = raw.substr(eq + 1);

            if (key == "filetype" || key == "ft") {
                if (value.empty()) {
                    filetype_.clear();
                    refreshSyntax();
                    return true;
                }
                if (!syntax_.knows(value)) return false;
                filetype_ = value;
                refreshSyntax();
                return true;
            }

            const bool digits = !value.empty() && value.size() <= 3 &&
                                std::all_of(value.begin(), value.end(), [](char c) {
                                    return std::isdigit(static_cast<unsigned char>(c)) != 0;
                                });
            if (!digits) return false;
            const int n = std::stoi(value);

            if (key == "tabstop" || key == "ts") {
                if (n < 1 || n > 32) return false;
                setTabStop(n);
                return true;
            }
            if (key == "shiftwidth" || key == "sw") {
                if (n > 32) return false;
                shiftwidth_ = n;
                return true;
            }
            return false;
        }

        std::string name = raw;
        bool toggle = false;
        bool value = true;

        if (!name.empty() && name.back() == '!') {
            toggle = true;
            name.pop_back();
        }
        if (!toggle && name.size() > 2 && name.compare(0, 2, "no") == 0) {
            value = false;
            name = name.substr(2);
        }

        bool *target = nullptr;
        if (name == "number" || name == "nu") {
            target = &number_;
        } else if (name == "relativenumber" || name == "rnu") {
            target = &relative_;
        } else if (name == "wrap") {
            target = &wrap_;
        } else if (name == "bidi") {
            target = &bidi_;
        } else if (name == "mouse") {
            target = &mouse_;
        } else if (name == "expandtab" || name == "et") {
            target = &expandtab_;
        } else if (name == "autoindent" || name == "ai") {
            target = &autoindent_;
        } else if (name == "syntax") {
            const bool current = syntax_.enabled();
            syntax_.setEnabled(toggle ? !current : value);
            return true;
        } else {
            return false;
        }

        *target = toggle ? !*target : value;
        return true;
    }

    std::string Editor::optionSummary() const {
        std::string text;
        text += number_ ? "number" : "nonumber";
        text += relative_ ? " relativenumber" : " norelativenumber";
        text += wrap_ ? " wrap" : " nowrap";
        text += syntax_.enabled() ? " syntax" : " nosyntax";
        text += expandtab_ ? " expandtab" : " noexpandtab";
        text += autoindent_ ? " autoindent" : " noautoindent";
        text += " ts=" + std::to_string(tabStop());
        text += " sw=" + std::to_string(shiftWidth());
        text += " ft=" + syntax_.languageName();
        text += bidi_ ? " bidi" : " nobidi";
        text += mouse_ ? " mouse" : " nomouse";
        return text;
    }

    void Editor::refreshSyntax() {
        syntax_.setFile(path_);
        if (!filetype_.empty()) {
            syntax_.setLanguage(filetype_);
        } else {
            syntax_.sniff(buffer_->line(0));
        }
    }

    std::size_t Editor::maxColumn(std::size_t row) const {
        const std::string &line = buffer_->line(row);
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