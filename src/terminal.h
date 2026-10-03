#pragma once

#include <termios.h>

#include <optional>

namespace qalam {

    class RawMode {
    public:
        RawMode();
        ~RawMode();

        RawMode(const RawMode&) = delete;
        RawMode& operator=(const RawMode&) = delete;

    private:
        termios orig_{};
    };

    std::optional<unsigned char> readByte();

}