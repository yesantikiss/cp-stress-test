// adaptive.hpp のテスト: g++ -std=gnu++20 -I.. tests/adaptive_test.cpp
// 引数を付けると、名前にその文字列を含むテストだけ実行する。
#include "stress/adaptive.hpp"
#include "tests/check.hpp"

#include <numeric>
#include <set>

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
                              });
}
