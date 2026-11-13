#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "buffer/text_buffer.h"

namespace qalam {
    enum class Kind : unsigned char { Normal, Keyword, Type, String, Comment, Number, Preproc };

    constexpr std::size_t kKindCount = 7;

    struct Language;

    class Syntax {
    public:
        struct State {
            unsigned char mode = 0;
            std::string delim;
            bool operator==(const State&) const = default;
        };

        Syntax();

        void setFile(const std::string &path);

        void sniff(const std::string &firstLine);

        bool setLanguage(const std::string &name);

        bool knows(const std::string &name) const;

        std::string languageName() const;

        void setEnabled(bool enabled);

        bool enabled() const;

        bool active() const;

        bool setColor(const std::string &name, int code);

        void resetColors();

        void invalidate(std::size_t row);

        void edited(const TextBuffer& buffer, std::size_t row, std::size_t removed,
            std::size_t added);

        const std::vector<Kind> &kinds(const TextBuffer &buffer, std::size_t row) const;

        std::string sgr(Kind kind) const;

    private:
        const Language *lang_ = nullptr;
        bool enabled_ = true;
        std::array<int, kKindCount> colors_;
        mutable std::vector<State> starts_;
        mutable std::vector<Kind> scratch_;
    };
}
