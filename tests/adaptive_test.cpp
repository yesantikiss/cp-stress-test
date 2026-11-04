// adaptive.hpp のテスト: g++ -std=gnu++20 -I.. tests/adaptive_test.cpp
#include "stress/adaptive.hpp"

#include <cassert>
#include <numeric>
#include <set>

using namespace stress;

int main() {
    // 候補の個数と中身
    assert(cand::range(3, 7).size() == 5);
    assert(cand::tuples(3, 0, 1).size() == 8);
    assert(cand::tuples(0, 1, 5).size() == 1);
    assert((cand::strings(2, "ab") == std::vector<std::string>{"aa", "ab", "ba", "bb"}));
    assert(cand::permutations(5).size() == 120);
    assert(cand::permutations(0).size() == 1);
    assert(cand::combinations(6, 3).size() == 20);
    assert(cand::combinations(4, 0).size() == 1);
    for (const auto& c : cand::combinations(6, 3)) assert(std::is_sorted(c.begin(), c.end()) && c.front() >= 1 && c.back() <= 6);
    const std::vector<long long> tree_count = {1, 1, 1, 3, 16, 125, 1296, 16807};
    for (int n = 1; n <= 7; ++n) {
        auto ts = cand::trees(n);
        assert((long long)ts.size() == tree_count[n]);
        std::set<std::vector<std::pair<int, int>>> uniq(ts.begin(), ts.end());
        assert(uniq.size() == ts.size());
        for (const auto& t : ts) {  // 木になっているか
            assert((int)t.size() == n - 1);
            std::vector<int> p(n + 1);
            std::iota(p.begin(), p.end(), 0);
            auto f = [&](auto self, int x) -> int { return p[x] == x ? x : p[x] = self(self, p[x]); };
            for (auto [u, v] : t) {
                assert(1 <= u && u < v && v <= n);
                assert(f(f, u) != f(f, v));
                p[f(f, u)] = f(f, v);
            }
        }
    }

    // 数当て: 一番多く残る返事を選ぶので、二分探索でちょうど ceil(log2 n) 回かかる
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        int n = 100;
        Adaptive ad(cand::range(1, n), seed);
        int lo = 1, hi = n, q = 0;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            ++q;
            if (ad.reply([&](long long x) { return x <= mid; })) hi = mid; else lo = mid + 1;
            assert((int)ad.size() == hi - lo + 1);
        }
        assert(q == 7 && ad.determined() && ad.only() == lo);
    }

    // 3 択の返事、同数なら seed で選ばれる（同じ seed なら同じ）
    {
        auto run = [](std::uint64_t seed) {
            Adaptive ad(cand::range(0, 8), seed);
            return ad.reply([](long long x) { return x % 3; });
        };
        std::set<long long> seen;
        for (std::uint64_t s = 0; s < 50; ++s) {
            assert(run(s) == run(s));
            seen.insert(run(s));
        }
        assert(seen.size() == 3);
    }

    // 種類が多い返事（map に切り替わる）
    {
        Adaptive ad(cand::range(0, 999), 1);
        auto r = ad.reply([](long long x) { return x % 100 < 50 ? x % 50 : 0; });
        assert(r == 0 && ad.size() == 510);
    }

    // fixed: 最初に選んだ 1 つに従う
    for (std::uint64_t seed = 0; seed < 20; ++seed) {
        Adaptive ad(cand::permutations(4), seed, Strategy::fixed);
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j) ad.reply([&](const std::vector<int>& p) { return p[i] < p[j]; });
        assert(ad.determined());
    }

    // 状態と点数の版: [lo, hi] を状態にした数当て
    {
        AdaptiveState st(std::pair<int, int>{1, 100}, 3);
        int q = 0;
        while (st.state().first < st.state().second) {
            int mid = (st.state().first + st.state().second) / 2;
            ++q;
            st.reply({true, false},
                     [&](std::pair<int, int>& s, bool yes) {
                         if (yes) s.second = std::min(s.second, mid); else s.first = std::max(s.first, mid + 1);
                         return s.first <= s.second;
                     },
                     [](const std::pair<int, int>& s) { return s.second - s.first + 1; });
        }
        assert(q == 7);
    }

    std::puts("adaptive_test ok");
}
