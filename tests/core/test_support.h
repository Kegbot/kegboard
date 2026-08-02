#pragma once

// A deliberately tiny test harness.
//
// Vendoring gtest or doctest would mean either a submodule or a network fetch
// in CI, for a suite that needs assertions and a pass/fail count. This is the
// whole framework.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace kbtest {

inline int &failures() {
  static int n = 0;
  return n;
}

inline int &checks() {
  static int n = 0;
  return n;
}

inline const char *&current_test() {
  static const char *name = "";
  return name;
}

inline void fail(const char *file, int line, const std::string &message) {
  failures()++;
  fprintf(stderr, "  FAIL %s:%d [%s]\n    %s\n", file, line, current_test(), message.c_str());
}

inline std::string show(const std::string &v) { return "\"" + v + "\""; }
inline std::string show(const char *v) { return show(std::string(v)); }
inline std::string show(bool v) { return v ? "true" : "false"; }
template<typename T> inline std::string show(T v) { return std::to_string(v); }

template<typename A, typename B>
inline void check_eq(const A &a, const B &b, const char *expr, const char *file, int line) {
  checks()++;
  if (!(a == b))
    fail(file, line, std::string(expr) + "\n      expected: " + show(b) + "\n      actual:   " + show(a));
}

inline void check_near(float a, float b, float tol, const char *expr, const char *file, int line) {
  checks()++;
  if (std::fabs(a - b) > tol)
    fail(file, line,
         std::string(expr) + "\n      expected: " + show(b) + " (+/- " + show(tol) + ")\n      actual:   " + show(a));
}

inline void check_true(bool v, const char *expr, const char *file, int line) {
  checks()++;
  if (!v)
    fail(file, line, std::string(expr) + " was false");
}

}  // namespace kbtest

#define CHECK_EQ(a, b) ::kbtest::check_eq((a), (b), #a " == " #b, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol) ::kbtest::check_near((a), (b), (tol), #a " ~= " #b, __FILE__, __LINE__)
#define CHECK(a) ::kbtest::check_true((a), #a, __FILE__, __LINE__)
#define CHECK_FALSE(a) ::kbtest::check_true(!(a), "!(" #a ")", __FILE__, __LINE__)

#define TEST(name) \
  static void name(); \
  static void run_##name() { \
    ::kbtest::current_test() = #name; \
    printf("  %s\n", #name); \
    name(); \
  } \
  static void name()

#define RUN(name) run_##name()

#define TEST_MAIN(suite_name, body) \
  int main() { \
    printf("%s\n", suite_name); \
    body; \
    printf("%s: %d checks, %d failures\n\n", suite_name, ::kbtest::checks(), ::kbtest::failures()); \
    return ::kbtest::failures() == 0 ? 0 : 1; \
  }
