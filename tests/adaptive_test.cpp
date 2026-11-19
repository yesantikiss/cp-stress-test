// adaptive.hpp のテスト: g++ -std=gnu++20 -I.. tests/adaptive_test.cpp
// 引数を付けると、名前にその文字列を含むテストだけ実行する。
#include "stress/adaptive.hpp"
#include "tests/check.hpp"

#include <fcntl.h>
#include <numeric>
#include <set>
#include <sys/wait.h>
#include <unistd.h>

using namespace stress;

static void test_candidates() {  // 候補の個数と中身
    CHECK(cand::range(3, 7).size() == 5);
    CHECK(cand::tuples(3, 0, 1).size() == 8);
    CHECK(cand::tuples(0, 1, 5).size() == 1);
    CHECK((cand::strings(2, "ab") == std::vector<std::string>{"aa", "ab", "ba", "bb"}));
    CHECK(cand::permutations(5).size() == 120);
    CHECK(cand::permutations(0).size() == 1);
    CHECK(cand::combinations(6, 3).size() == 20);
    CHECK(cand::combinations(4, 0).size() == 1);
    for (const auto& c : cand::combinations(6, 3))
        CHECK(std::is_sorted(c.begin(), c.end()) && c.front() >= 1 && c.back() <= 6);
}

static void test_trees() {  // 木の個数（n^(n-2)）と、どれも木になっているか
    const std::vector<long long> tree_count = {1, 1, 1, 3, 16, 125, 1296, 16807};
    for (int n = 1; n <= 7; ++n) {
        auto ts = cand::trees(n);
        CHECK((long long)ts.size() == tree_count[n]);
        std::set<std::vector<std::pair<int, int>>> uniq(ts.begin(), ts.end());
        CHECK(uniq.size() == ts.size());
        for (const auto& t : ts) {
            CHECK_OR((int)t.size() == n - 1, continue);
            std::vector<int> p(n + 1);
            std::iota(p.begin(), p.end(), 0);
            auto f = [&](auto self, int x) -> int { return p[x] == x ? x : p[x] = self(self, p[x]); };
            for (auto [u, v] : t) {
                CHECK_OR(1 <= u && u < v && v <= n, break);
                CHECK(f(f, u) != f(f, v));
                p[f(f, u)] = f(f, v);
            }
        }
    }
    // kMaxCandidates (10^7) を超える n = 10 では interactor 失敗（終了コード 3）になる
    std::fflush(stdout);
    std::fflush(stderr);
    const pid_t pid = fork();
    if (pid == 0) {
        const int fd = open("/dev/null", O_WRONLY);
        dup2(fd, 2);
        cand::trees(10);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 3);
}

static void test_binary_search() {  // 一番多く残る返事を選ぶので、二分探索でちょうど ceil(log2 n) 回かかる
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        int n = 100;
        Adaptive ad(cand::range(1, n), seed);
        int lo = 1, hi = n, q = 0;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            ++q;
            if (ad.reply([&](long long x) { return x <= mid; })) hi = mid; else lo = mid + 1;
            CHECK((int)ad.size() == hi - lo + 1);
        }
        CHECK(q == 7 && ad.determined() && ad.only() == lo);
    }
}

static void test_tie_by_seed() {  // 3 択の返事、同数なら seed で選ばれる（同じ seed なら同じ）
    auto run = [](std::uint64_t seed) {
        Adaptive ad(cand::range(0, 8), seed);
        return ad.reply([](long long x) { return x % 3; });
    };
    std::set<long long> seen;
    for (std::uint64_t s = 0; s < 50; ++s) {
        CHECK(run(s) == run(s));
        seen.insert(run(s));
    }
    CHECK(seen.size() == 3);
}

static void test_many_replies() {  // 種類が多い返事（map に切り替わる）
    Adaptive ad(cand::range(0, 999), 1);
    auto r = ad.reply([](long long x) { return x % 100 < 50 ? x % 50 : 0; });
    CHECK(r == 0 && ad.size() == 510);
}

static void test_fixed() {  // fixed: 最初に選んだ 1 つに従う
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        Adaptive ad(cand::permutations(4), seed, Strategy::fixed);
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j) ad.reply([&](const std::vector<int>& p) { return p[i] < p[j]; });
        CHECK(ad.determined());
    }
}

static void test_state() {  // 状態と点数の版: [lo, hi] を状態にした数当て
    AdaptiveState st(std::pair<int, int>{1, 100}, 3);
    int q = 0;
    while (st.state().first < st.state().second && q < 100) {
        int mid = (st.state().first + st.state().second) / 2;
        ++q;
        st.reply({true, false},
                 [&](std::pair<int, int>& s, bool yes) {
                     if (yes) s.second = std::min(s.second, mid); else s.first = std::max(s.first, mid + 1);
                     return s.first <= s.second;
                 },
                 [](const std::pair<int, int>& s) { return s.second - s.first + 1; });
    }
    CHECK(q == 7);
}

static void test_to_text() {  // string_view も 1 文字ずつに分解せず文字列として出す
    CHECK(adaptive_detail::to_text(std::string_view("Yes")) == "Yes");
    CHECK(adaptive_detail::to_text(std::string("No")) == "No");
    CHECK(adaptive_detail::to_text("abc") == "abc");
    CHECK(adaptive_detail::to_text(std::vector<std::string_view>{"Yes", "No"}) == "[Yes, No]");
    CHECK(adaptive_detail::to_text(std::pair<std::string_view, int>{"ok", 3}) == "(ok, 3)");
}

static void test_reader() {  // long double 精度、float の範囲外、read_u64 の先頭ゼロと範囲外
    auto read_ok = [](std::string_view s, auto fn) {
        int p[2];
        CHECK_OR(pipe(p) == 0, return);
        std::size_t off = 0;
        while (off < s.size()) {
            ssize_t n = write(p[1], s.data() + off, s.size() - off);
            if (n <= 0) break;
            off += static_cast<std::size_t>(n);
        }
        close(p[1]);
        Reader r = Reader::stream(p[0], "output", kWrong);
        fn(r);
        close(p[0]);
    };
    auto read_fails = [](std::string_view s, auto fn, std::string_view err_sub = "") {
        std::fflush(stdout);
        std::fflush(stderr);
        int ep[2];
        if (pipe(ep) != 0) return false;
        const pid_t pid = fork();
        if (pid == 0) {
            close(ep[0]);
            dup2(ep[1], 2);
            close(ep[1]);
            int ip[2];
            if (pipe(ip) != 0) _exit(99);
            ssize_t ignored = write(ip[1], s.data(), s.size());
            (void)ignored;
            close(ip[1]);
            Reader r = Reader::stream(ip[0], "output", kWrong);
            fn(r);
            _exit(0);
        }
        close(ep[1]);
        std::string err;
        char buf[256];
        for (ssize_t n; (n = read(ep[0], buf, sizeof(buf))) > 0;) err.append(buf, static_cast<std::size_t>(n));
        close(ep[0]);
        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == kWrong &&
               (err_sub.empty() || err.find(err_sub) != std::string::npos);
    };

    // float / double / long double
    read_ok("3.25 1e-100 1.5 1e-400", [](Reader& r) {
        float f1 = 0, f2 = 1;
        double d1 = 0, d2 = 1;
        r >> f1 >> f2 >> d1 >> d2;
        CHECK(f1 == 3.25f && f2 == 0.0f && d1 == 1.5 && d2 == 0.0);
        r.expect_eof();
    });
    if constexpr (std::numeric_limits<long double>::digits10 >= 18) {
        read_ok("1.000000000000000001", [](Reader& r) {
            long double ld = 0;
            r >> ld;
            CHECK(ld != 1.0L && std::fabs(ld - 1.000000000000000001L) < 1e-18L);
        });
    }
    if constexpr (std::numeric_limits<long double>::max_exponent10 >= 1000) {
        read_ok("1e1000", [](Reader& r) {
            long double ld = 0;
            r >> ld;
            CHECK(std::isfinite(ld) && ld > 1e308L);
        });
    }
    CHECK(read_fails("1e100", [](Reader& r) { float f; r >> f; }));
    CHECK(read_fails("-1e100", [](Reader& r) { float f; r >> f; }));
    CHECK(read_fails("inf", [](Reader& r) { float f; r >> f; }));
    CHECK(read_fails("nan", [](Reader& r) { float f; r >> f; }));
    CHECK(read_fails("1e400", [](Reader& r) { double d; r >> d; }));
    CHECK(read_fails("1e10000", [](Reader& r) { long double ld; r >> ld; }));

    // read_u64: 先頭ゼロを含む 21 桁以上でも 64 bit に収まれば読める。範囲外は out of 64-bit range
    read_ok("0000000000000000000001 0000000000000000000000000 00018446744073709551615", [](Reader& r) {
        unsigned long long a = 0, b = 1, c = 0;
        r >> a >> b >> c;
        CHECK(a == 1ULL && b == 0ULL && c == std::numeric_limits<unsigned long long>::max());
        r.expect_eof();
    });
    CHECK(read_fails("18446744073709551616", [](Reader& r) { (void)r.read_u64(); }, "out of 64-bit range"));
    CHECK(read_fails("100000000000000000000", [](Reader& r) { (void)r.read_u64(); }, "out of 64-bit range"));
    CHECK(read_fails("+1", [](Reader& r) { (void)r.read_u64(); }, "expected an unsigned integer"));
    CHECK(read_fails("-1", [](Reader& r) { (void)r.read_u64(); }, "expected an unsigned integer"));
    CHECK(read_fails("12a", [](Reader& r) { (void)r.read_u64(); }, "expected an unsigned integer"));
}

int main(int argc, char** argv) {
    return testing::run_tests(argc, argv,
                              {
                                  {"candidates", test_candidates},
                                  {"trees", test_trees},
                                  {"binary_search", test_binary_search},
                                  {"tie_by_seed", test_tie_by_seed},
                                  {"many_replies", test_many_replies},
                                  {"fixed", test_fixed},
                                  {"state", test_state},
                                  {"to_text", test_to_text},
                                  {"reader", test_reader},
                              });
}
