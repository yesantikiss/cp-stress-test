#pragma once

// テスト用の小さな道具。
//   CHECK(式): 偽なら「テスト名: ファイル:行 式」を出して続ける（abort しない）。
//   CHECK_OR(式, 文): 偽なら CHECK と同じく記録して、文（return や break）を実行する。
//                    添字が範囲外になるときなど、先に進むと壊れるところで使う。
//   run_tests(argc, argv, {{"名前", 関数}, ...}): 順に実行し、テストごとに ok / FAIL を出す。
//                    引数を付けると、名前にその文字列を含むテストだけ実行する。
// 終了コードは失敗があれば 1。

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace testing {

inline int& fails() {
    static int n = 0;
    return n;
}
inline const char*& current() {
    static const char* name = "";
    return name;
}

inline void report(const char* file, int line, const char* expr) {
    // 同じテストで大量に落ちたときは最初の 5 個だけ出す
    if (++fails() <= 5) std::printf("  %s: %s:%d: %s\n", current(), file, line, expr);
    std::fflush(stdout);
}

using Test = std::pair<const char*, std::function<void()>>;

inline int run_tests(int argc, char** argv, const std::vector<Test>& tests) {
    int failed = 0, ran = 0;
    for (const auto& [name, f] : tests) {
        if (argc > 1 && !std::strstr(name, argv[1])) continue;
        current() = name;
        fails() = 0;
        f();
        ++ran;
        if (fails() == 0) {
            std::printf("ok   %s\n", name);
        } else {
            std::printf("FAIL %s (%d checks failed)\n", name, fails());
            ++failed;
        }
        std::fflush(stdout);
    }
    std::printf("%d tests, %d failed\n", ran, failed);
    return failed ? 1 : 0;
}

}  // namespace testing

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) ::testing::report(__FILE__, __LINE__, #cond);           \
    } while (0)

#define CHECK_OR(cond, action)                                               \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ::testing::report(__FILE__, __LINE__, #cond);                    \
            action;                                                          \
        }                                                                    \
    } while (0)
