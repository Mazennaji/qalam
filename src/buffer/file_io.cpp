#include "buffer/file_io.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace qalam {
    namespace {
        namespace fs = std::filesystem;

        bool allCrlf(const std::string &data) {
            bool any = false;
            for (std::size_t i = 0; i < data.size(); ++i) {
                if (data[i] != '\n') continue;
                if (i == 0 || data[i - 1] != '\r') return false;
                any = true;
            }
            return any;
        }

        std::string serialize(const TextBuffer &buffer, bool crlf) {
            const std::size_t count = buffer.lineCount();
            if (count == 1 && buffer.line(0).empty()) return "";

            std::string out;
            for (std::size_t i = 0; i < count; ++i) {
                out += buffer.line(i);
                out += crlf ? "\r\n" : "\n";
            }
            return out;
        }

        bool writeAll(const std::string &path, const std::string &data) {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
            out.flush();
            return static_cast<bool>(out);
        }
    }

    LoadResult loadFile(const std::string &path, TextBuffer &buffer) {
        LoadResult result;

        std::error_code ec;
        if (!fs::exists(path, ec)) {
            result.status = LoadStatus::NotFound;
            return result;
        }
        if (fs::is_directory(path, ec)) return result;

        std::ifstream in(path, std::ios::binary);
        if (!in) return result;

        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (in.bad()) return result;

        result.bytes = data.size();
        result.crlf = allCrlf(data);

        if (result.crlf) {
            std::string clean;
            clean.reserve(data.size());
            for (std::size_t i = 0; i < data.size(); ++i) {
                if (data[i] == '\r' && i + 1 < data.size() && data[i + 1] == '\n') continue;
                clean += data[i];
            }
            data.swap(clean);
        }

        if (!data.empty() && data.back() == '\n') data.pop_back();

        const std::size_t last = buffer.lineCount() - 1;
        static_cast<void>(buffer.replace({0, 0}, {last, buffer.line(last).size()}, data));

        result.status = LoadStatus::Loaded;
        return result;
    }

    std::optional<std::size_t> saveFile(const std::string &path, const TextBuffer &buffer,
                                        bool crlf) {
        const std::string data = serialize(buffer, crlf);

        std::error_code ec;
        fs::path target(path);
        if (fs::is_symlink(target, ec)) {
            const fs::path resolved = fs::canonical(target, ec);
            if (!ec) target = resolved;
        }

        const fs::path temp = target.string() + ".qalam-tmp";
        if (writeAll(temp.string(), data)) {
            std::error_code perms;
            if (fs::exists(target, perms)) {
                fs::permissions(temp, fs::status(target, perms).permissions(),
                                fs::perm_options::replace, perms);
            }
            std::error_code rename;
            fs::rename(temp, target, rename);
            if (!rename) return data.size();
            fs::remove(temp, rename);
        } else {
            std::error_code cleanup;
            fs::remove(temp, cleanup);
        }

        if (fs::exists(target, ec) && writeAll(target.string(), data)) return data.size();
        return std::nullopt;
    }
}
