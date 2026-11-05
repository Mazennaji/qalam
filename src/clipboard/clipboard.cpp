#include "clipboard/clipboard.h"

#include <cstdio>
#include <cstdlib>

#include "terminal/terminal.h"

namespace qalam {
namespace {

struct Tool {
    const char* probe;
    const char* copy;
    const char* paste;
    bool windows;
};

const Tool* detect() {
    static const Tool tools[] = {
        {"wl-copy", "wl-copy", "wl-paste --no-newline", false},
        {"xclip", "xclip -selection clipboard -in", "xclip -selection clipboard -out", false},
        {"xsel", "xsel --clipboard --input", "xsel --clipboard --output", false},
        {"pbcopy", "pbcopy", "pbpaste", false},
        {"clip.exe", "clip.exe", "powershell.exe -NoProfile -Command Get-Clipboard -Raw", true},
    };

    static const Tool* found = nullptr;
    static bool done = false;
    if (!done) {
        done = true;
        for (const Tool& tool : tools) {
            const std::string probe = std::string("command -v ") + tool.probe + " >/dev/null 2>&1";
            if (std::system(probe.c_str()) == 0) {
                found = &tool;
                break;
            }
        }
    }
    return found;
}

std::string base64(const std::string& data) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);

    std::size_t i = 0;
    while (i + 2 < data.size()) {
        const unsigned v = (static_cast<unsigned char>(data[i]) << 16) |
                           (static_cast<unsigned char>(data[i + 1]) << 8) |
                           static_cast<unsigned char>(data[i + 2]);
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += alphabet[(v >> 6) & 63];
        out += alphabet[v & 63];
        i += 3;
    }

    const std::size_t left = data.size() - i;
    if (left == 1) {
        const unsigned v = static_cast<unsigned char>(data[i]) << 16;
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += "==";
    } else if (left == 2) {
        const unsigned v = (static_cast<unsigned char>(data[i]) << 16) |
                           (static_cast<unsigned char>(data[i + 1]) << 8);
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += alphabet[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

}  // namespace

bool clipboardWrite(const std::string& text) {
    if (const Tool* tool = detect()) {
        const std::string command = std::string(tool->copy) + " >/dev/null 2>&1";
        if (FILE* pipe = popen(command.c_str(), "w")) {
            const std::size_t written = std::fwrite(text.data(), 1, text.size(), pipe);
            const int status = pclose(pipe);
            if (written == text.size() && status == 0) return true;
        }
    }

    if (text.size() > 100000) return false;
    writeAll("\x1b]52;c;" + base64(text) + "\x07");
    return true;
}

std::optional<std::string> clipboardRead() {
    const Tool* tool = detect();
    if (tool == nullptr) return std::nullopt;

    const std::string command = std::string(tool->paste) + " 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) return std::nullopt;

    std::string text;
    char chunk[4096];
    std::size_t n = 0;
    while ((n = std::fread(chunk, 1, sizeof chunk, pipe)) > 0) text.append(chunk, n);
    if (pclose(pipe) != 0) return std::nullopt;

    if (tool->windows) {
        std::string clean;
        for (char c : text) {
            if (c != '\r') clean += c;
        }
        if (!clean.empty() && clean.back() == '\n') clean.pop_back();
        text = clean;
    }
    return text;
}

}