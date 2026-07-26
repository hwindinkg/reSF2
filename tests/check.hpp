// tests/check.hpp
//
// Minimal assertion helpers. The point is that a check that fails must make
// the process exit non-zero — several tests in this suite used to print
// "FAILED" and then `return 0`, so CTest reported them green.

#pragma once

#include <cstdio>
#include <string>

namespace resf2::test {

inline int g_checks = 0;
inline int g_failed = 0;

inline void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++g_failed;
    }
}

template <typename A, typename B>
inline void check_eq(const A& got, const B& want, const std::string& what) {
    ++g_checks;
    if (!(got == want)) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++g_failed;
    }
}

inline void check_ge(double got, double want, const std::string& what) {
    ++g_checks;
    if (!(got >= want)) {
        std::fprintf(stderr, "FAIL: %s — got %.3f, want >= %.3f\n",
                     what.c_str(), got, want);
        ++g_failed;
    }
}

inline void check_near(double got, double want, double tol, const std::string& what) {
    ++g_checks;
    const double d = got - want;
    if (!(d <= tol && -d <= tol)) {
        std::fprintf(stderr, "FAIL: %s — got %.3f, want %.3f +-%.3f\n",
                     what.c_str(), got, want, tol);
        ++g_failed;
    }
}

// Use as `return resf2::test::summary();` from main().
inline int summary() {
    if (g_failed) {
        std::fprintf(stderr, "\n%d of %d checks failed\n", g_failed, g_checks);
        return 1;
    }
    std::printf("\nall %d checks passed\n", g_checks);
    return 0;
}

}  // namespace resf2::test
