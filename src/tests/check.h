#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace qt {
    struct Case {
        std::string name;
        std::function<void()> fn;
    };

    inline std::vector<Case> &registry() {
        static std::vector<Case> cases;
        return cases;
    }

    inline int &failures() {
        static int count = 0;
        return count;
    }

    struct Registrar {
        Registrar(const char *name, std::function<void()> fn) {
            registry().push_back({name, std::move(fn)});
        }
    };

    template<typename A, typename B>
    void checkEq(const A &a, const B &b, const char *ea, const char *eb, const char *file, int line) {
        if (a == b) return;
        ++failures();
        std::ostringstream out;
        out << file << ":" << line << ": CHECK_EQ(" << ea << ", " << eb << ") failed\n"
                << "    left:  " << a << "\n    right: " << b << "\n";
        std::cerr << out.str();
    }
} // namespace qt

#define TEST(name)                                                  \
static void test_##name();                                      \
static qt::Registrar registrar_##name(#name, test_##name);      \
static void test_##name()

#define CHECK(expr)                                                              \
do {                                                                         \
if (!(expr)) {                                                           \
++qt::failures();                                                    \
std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK(" #expr ") failed\n"; \
}                                                                        \
} while (0)

#define CHECK_EQ(a, b) qt::checkEq((a), (b), #a, #b, __FILE__, __LINE__)
