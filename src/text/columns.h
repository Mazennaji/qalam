#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace qalam {

    constexpr int kTabStop = 4;

    std::size_t charLength(const std::string& line, std::size_t index);
    std::size_t prevCharStart(const std::string& line, std::size_t index);
    std::size_t snapToStart(const std::string& line, std::size_t index);

    int displayColumn(const std::string& line, std::size_t index);
    int displayWidth(const std::string& line);
    std::size_t byteForColumn(const std::string& line, std::size_t column);
    std::pair<std::size_t, std::size_t> byteSpan(const std::string& line, std::size_t left,
                                                 std::size_t right);

    std::string truncateToWidth(const std::string& text, std::size_t width);
    std::vector<std::string> displayCells(const std::string& line);
    std::vector<std::size_t> cellSources(const std::string& line);
    int wrapHeight(const std::string& line, int width);
    void popLastChar(std::string& text);

}  // namespace qalam