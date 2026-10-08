#pragma once

// Minimal test helpers: no framework, since the spec allows no third-party
// libraries. Each test program returns 0 when every check passed.

#include <iostream>

namespace testutil {

inline int checks = 0;
inline int failures = 0;

inline void check(bool ok, const char* expr, const char* file, int line) {
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << file << ':' << line << ": CHECK failed: " << expr << '\n';
    }
}

template <typename A, typename B>
void checkEq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
    ++checks;
    if (!(a == b)) {
        ++failures;
        std::cerr << file << ':' << line << ": CHECK_EQ failed: " << ea << " == " << eb << "  (" << a << " vs "
                  << b << ")\n";
    }
}

inline int finish(const char* name) {
    std::cout << name << ": " << (checks - failures) << '/' << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}

}  // namespace testutil

#define CHECK(expr) ::testutil::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) ::testutil::checkEq((a), (b), #a, #b, __FILE__, __LINE__)
