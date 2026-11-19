#pragma once
#include <iostream>
#include <string>

namespace Check {
    inline int& Failures() { static int n = 0; return n; }

    inline void Expect(bool ok, const char* expr, const char* file, int line) {
        if (!ok) { ++Failures(); std::cerr << "[FAIL] " << file << ":" << line << "  " << expr << "\n"; }
    }

    template <typename A, typename B>
    void ExpectEq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
        if (!(a == b)) {
            ++Failures();
            std::cerr << "[FAIL] " << file << ":" << line << "  " << ea << " == " << eb
                      << "  (got " << a << " vs " << b << ")\n";
        }
    }
}

#define CHECK(x)        ::Check::Expect((x), #x, __FILE__, __LINE__)
#define CHECK_EQ(a, b)  ::Check::ExpectEq((a), (b), #a, #b, __FILE__, __LINE__)
