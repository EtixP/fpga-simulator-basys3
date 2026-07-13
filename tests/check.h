// Test assertions that survive every build type. NEVER use <cassert> in
// tests: Release/RelWithDebInfo define NDEBUG, assert() compiles out, and the
// whole suite goes vacuously green.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      std::fprintf(stderr, "CHECK failed: %s\n  at %s:%d\n", #cond, __FILE__,    \
                   __LINE__);                                                    \
      std::exit(1);                                                              \
    }                                                                            \
  } while (0)

#define CHECK_EQ(a, b)                                                           \
  do {                                                                           \
    const auto va = (a);                                                         \
    const auto vb_ = (b);                                                        \
    if (!(va == vb_)) {                                                          \
      std::fprintf(stderr,                                                       \
                   "CHECK_EQ failed: %s == %s\n  actual:   %llu\n  expected: "   \
                   "%llu\n  at %s:%d\n",                                         \
                   #a, #b, (unsigned long long)(va), (unsigned long long)(vb_),  \
                   __FILE__, __LINE__);                                          \
      std::exit(1);                                                              \
    }                                                                            \
  } while (0)

#define CHECK_THROWS(expr, ExType)                                               \
  do {                                                                           \
    bool caught_ = false;                                                        \
    try {                                                                        \
      (void)(expr);                                                              \
    } catch (const ExType&) {                                                    \
      caught_ = true;                                                            \
    }                                                                            \
    if (!caught_) {                                                              \
      std::fprintf(stderr, "CHECK_THROWS failed: %s did not throw %s\n  at %s:%d\n", \
                   #expr, #ExType, __FILE__, __LINE__);                          \
      std::exit(1);                                                              \
    }                                                                            \
  } while (0)
