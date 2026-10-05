#include "buffer/file_io.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace qalam {

    LoadResult loadFile(const std::string& path, TextBuffer& buffer) {
        namespace fs = std::filesystem;
        std::error_code ec;

        if (!fs::exists(path, ec)) {
            buffer.assign(std::vector<std::string>{});
            return {LoadStatus::NotFound, 0};
        }
        if (!fs::is_regular_file(path, ec)) {
            return {LoadStatus::Failed, 0};
        }

        std::ifstream in(path, std::ios::binary);
        if (!in) return {LoadStatus::Failed, 0};

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(std::move(line));
            line.clear();
        }
        if (in.bad()) return {LoadStatus::Failed, 0};

        const auto size = fs::file_size(path, ec);
        buffer.assign(std::move(lines));
        return {LoadStatus::Loaded, ec ? 0 : static_cast<std::size_t>(size)};
    }

    std::optional<std::size_t> saveFile(const std::string& path, const TextBuffer& buffer) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return std::nullopt;

        std::size_t written = 0;
        const bool empty = buffer.lineCount() == 1 && buffer.line(0).empty();
        if (!empty) {
            for (std::size_t i = 0; i < buffer.lineCount(); ++i) {
                const std::string& text = buffer.line(i);
                out.write(text.data(), static_cast<std::streamsize>(text.size()));
                out.put('\n');
                written += text.size() + 1;
            }
        }

        out.flush();
        if (!out) return std::nullopt;
        return written;
    }

}