#include <cstring>
#include <iostream>

#include "check.h"

int main(int argc, char **argv) {
    const char *filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    int failed = 0;

    for (const qt::Case &test: qt::registry()) {
        if (filter != nullptr && test.name.find(filter) == std::string::npos) continue;

        const int before = qt::failures();
        test.fn();
        ++ran;

        if (qt::failures() != before) {
            ++failed;
            std::cerr << "FAIL  " << test.name << "\n";
        }
    }

    std::cout << ran << " tests, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
