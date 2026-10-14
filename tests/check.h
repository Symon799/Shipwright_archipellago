#pragma once

#include <cstdlib>
#include <iostream>

// Unlike assert, test checks (and their side effects) remain active in Release.
inline void TestCheck(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        std::cerr << file << ':' << line << ": CHECK failed: " << expression << '\n';
        std::exit(EXIT_FAILURE);
    }
}
#define CHECK(expression) TestCheck(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
