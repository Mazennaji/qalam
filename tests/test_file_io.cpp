#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "buffer/file_io.h"
#include "buffer/line_buffer.h"
#include "harness.h"

using namespace qalam;
namespace fs = std::filesystem;

namespace {
    std::string tmp(const std::string &name) {
        return (fs::temp_directory_path() / ("qalam_io_" + name)).string();
    }

    void write(const std::string &path, const std::string &data) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
    }

    std::string read(const std::string &path) {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    std::string joined(const LineBuffer &b) {
        return b.text({0, 0}, {b.lineCount() - 1, b.line(b.lineCount() - 1).size()});
    }
}

TEST(io_missing_file_is_not_found) {
    LineBuffer b;
    CHECK(loadFile(tmp("does_not_exist"), b).status == LoadStatus::NotFound);
}

TEST(io_directory_fails_to_load) {
    LineBuffer b;
    CHECK(loadFile(fs::temp_directory_path().string(), b).status == LoadStatus::Failed);
}

TEST(io_simple_round_trip) {
    const std::string path = tmp("simple");
    write(path, "one\ntwo\nthree\n");
    LineBuffer b;
    const LoadResult r = loadFile(path, b);
    CHECK(r.status == LoadStatus::Loaded);
    CHECK_EQ(r.bytes, std::size_t{14});
    CHECK_EQ(b.lineCount(), std::size_t{3});
    CHECK_EQ(b.line(2), std::string("three"));

    const auto saved = saveFile(path, b);
    CHECK(saved.has_value());
    CHECK_EQ(*saved, std::size_t{14});
    CHECK_EQ(read(path), std::string("one\ntwo\nthree\n"));
    fs::remove(path);
}

TEST(io_missing_final_newline_gets_one_on_save) {
    const std::string path = tmp("nofinal");
    write(path, "a\nb");
    LineBuffer b;
    loadFile(path, b);
    CHECK_EQ(b.lineCount(), std::size_t{2});
    saveFile(path, b);
    CHECK_EQ(read(path), std::string("a\nb\n"));
    fs::remove(path);
}

TEST(io_trailing_blank_line_is_kept) {
    const std::string path = tmp("blank");
    write(path, "a\n\n");
    LineBuffer b;
    loadFile(path, b);
    CHECK_EQ(b.lineCount(), std::size_t{2});
    CHECK(b.line(1).empty());
    saveFile(path, b);
    CHECK_EQ(read(path), std::string("a\n\n"));
    fs::remove(path);
}

TEST(io_empty_file) {
    const std::string path = tmp("empty");
    write(path, "");
    LineBuffer b;
    const LoadResult r = loadFile(path, b);
    CHECK(r.status == LoadStatus::Loaded);
    CHECK_EQ(r.bytes, std::size_t{0});
    CHECK_EQ(b.lineCount(), std::size_t{1});
    const auto saved = saveFile(path, b);
    CHECK(saved.has_value());
    CHECK_EQ(*saved, std::size_t{0});
    CHECK_EQ(read(path), std::string(""));
    fs::remove(path);
}

TEST(io_crlf_is_detected_stripped_and_restored) {
    const std::string path = tmp("crlf");
    write(path, "a\r\nb\r\n");
    LineBuffer b;
    const LoadResult r = loadFile(path, b);
    CHECK(r.crlf);
    CHECK_EQ(b.line(0), std::string("a"));
    CHECK_EQ(b.line(1), std::string("b"));
    saveFile(path, b, r.crlf);
    CHECK_EQ(read(path), std::string("a\r\nb\r\n"));
    fs::remove(path);
}

TEST(io_mixed_endings_are_left_alone) {
    const std::string path = tmp("mixed");
    write(path, "a\r\nb\nc\r\n");
    LineBuffer b;
    const LoadResult r = loadFile(path, b);
    CHECK(!r.crlf);
    CHECK_EQ(b.line(0), std::string("a\r"));
    CHECK_EQ(b.line(1), std::string("b"));
    saveFile(path, b, r.crlf);
    CHECK_EQ(read(path), std::string("a\r\nb\nc\r\n"));
    fs::remove(path);
}

TEST(io_binary_bytes_round_trip) {
    const std::string path = tmp("binary");
    std::string data("a\0b\n\xff\xfe\x80z\n", 9);
    write(path, data);
    LineBuffer b;
    loadFile(path, b);
    CHECK_EQ(b.line(0).size(), std::size_t{3});
    saveFile(path, b);
    CHECK_EQ(read(path), data);
    fs::remove(path);
}

TEST(io_utf8_round_trip) {
    const std::string path = tmp("utf8");
    const std::string data = "h\xc3\xa9llo\n\xe6\x97\xa5\xe6\x9c\xac\n";
    write(path, data);
    LineBuffer b;
    loadFile(path, b);
    saveFile(path, b);
    CHECK_EQ(read(path), data);
    fs::remove(path);
}

TEST(io_very_long_line) {
    const std::string path = tmp("long");
    const std::string line(1000000, 'x');
    write(path, line + "\nend\n");
    LineBuffer b;
    loadFile(path, b);
    CHECK_EQ(b.lineCount(), std::size_t{2});
    CHECK_EQ(b.line(0).size(), std::size_t{1000000});
    saveFile(path, b);
    CHECK_EQ(read(path), line + "\nend\n");
    fs::remove(path);
}

TEST(io_save_leaves_no_temp_file) {
    const std::string path = tmp("notemp");
    write(path, "x\n");
    LineBuffer b;
    loadFile(path, b);
    saveFile(path, b);
    CHECK(!fs::exists(path + ".qalam-tmp"));
    fs::remove(path);
}

TEST(io_save_to_bad_directory_fails) {
    LineBuffer b;
    CHECK(!saveFile("/nonexistent_qalam_dir/file.txt", b).has_value());
}

TEST(io_save_keeps_permissions) {
    const std::string path = tmp("perm");
    write(path, "#!/bin/sh\n");
    fs::permissions(path, fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read,
                    fs::perm_options::replace);
    LineBuffer b;
    loadFile(path, b);
    saveFile(path, b);
    const auto perms = fs::status(path).permissions();
    CHECK((perms & fs::perms::owner_exec) != fs::perms::none);
    fs::remove(path);
}

TEST(io_save_through_symlink_keeps_link) {
    const std::string target = tmp("link_target");
    const std::string link = tmp("link");
    fs::remove(link);
    write(target, "old\n");
    std::error_code ec;
    fs::create_symlink(target, link, ec);
    if (ec) {
        fs::remove(target);
        return;
    }
    LineBuffer b;
    loadFile(link, b);
    b.replace({0, 0}, {0, 3}, "new");
    saveFile(link, b);
    CHECK(fs::is_symlink(link));
    CHECK_EQ(read(target), std::string("new\n"));
    fs::remove(link);
    fs::remove(target);
}

TEST(io_editor_saves_crlf_files_as_crlf) {
    const std::string path = tmp("editor_crlf");
    write(path, "ab\r\ncd\r\n");
    Ed ed("");
    ed.e.open(path);
    CHECK_EQ(ed.text(), std::string("ab\ncd"));
    CHECK(ed.e.message().find("[dos]") != std::string::npos);
    ed.feed("x:w<CR>");
    CHECK_EQ(read(path), std::string("b\r\ncd\r\n"));
    fs::remove(path);
}

TEST(io_editor_unmodified_save_is_identical) {
    const std::string path = tmp("editor_same");
    write(path, "line one\n\tline two\n\nlast\n");
    Ed ed("");
    ed.e.open(path);
    ed.feed(":w<CR>");
    CHECK_EQ(read(path), std::string("line one\n\tline two\n\nlast\n"));
    fs::remove(path);
}
