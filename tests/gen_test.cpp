// gen.hpp の性質テスト: g++ -std=gnu++20 -I.. tests/gen_test.cpp
#define STRESS_NO_MAIN
#include "stress/gen.hpp"
#include <cassert>
#include <climits>
#include <map>

using namespace stress;

static bool is_tree(ll n, const Edges& es, ll base) {
    if ((ll)es.size() != n - 1) return false;
    std::vector<ll> p(n);
    std::iota(p.begin(), p.end(), 0);
    std::function<ll(ll)> f = [&](ll x) { return p[x] == x ? x : p[x] = f(p[x]); };
    for (auto& e : es) {
        ll u = e.u - base, v = e.v - base;
        if (u < 0 || u >= n || v < 0 || v >= n) return false;
        if (f(u) == f(v)) return false;
        p[f(u)] = f(v);
    }
    return true;
}

int main() {
    // 同じ seed なら同じ出力
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        Gen a(seed), b(seed);
        for (int k = 0; k < 100; ++k) assert(a.i(-5, 1000000) == b.i(-5, 1000000));
        auto s1 = a.str(30, "ab"), s2 = b.str(30, "ab");
        assert(s1 == s2);
    }
    Gen g(12345);
    // 範囲
    for (int k = 0; k < 100000; ++k) {
        ll x = g.i(-3, 7);
        assert(-3 <= x && x <= 7);
        ll y = g.uniform(LLONG_MIN, LLONG_MAX);
        (void)y;
    }
    // 端が出やすい
    {
        int hit = 0;
        for (int k = 0; k < 10000; ++k) hit += g.i(0, 1000000000) == 1000000000;
        assert(hit > 100);
    }
    // size は cap で切られる
    {
        Gen h(1, 3);
        for (int k = 0; k < 1000; ++k) {
            ll n = h.size(1, 100);
            assert(1 <= n && n <= 3);
        }
        Gen h2(1, 0);
        assert(h2.size(2, 100) == 2);  // lo は下回らない
    }
    // distinct / perm / partition
    for (int k = 0; k < 200; ++k) {
        ll n = g.i(0, 50);
        auto d = g.distinct(n, 10, 10 + n + g.i(0, 3));
        std::set<ll> st(d.begin(), d.end());
        assert((ll)st.size() == n);
        auto d2 = g.distinct(n, 0, 1000000000000LL);
        assert((ll)std::set<ll>(d2.begin(), d2.end()).size() == n);
        auto p = g.perm(n);
        std::sort(p.begin(), p.end());
        for (ll i = 0; i < n; ++i) assert(p[i] == i + 1);
        ll sum = g.i(n, 100), parts = g.i(1, 10);
        auto q = g.partition(sum, parts);
        assert(std::accumulate(q.begin(), q.end(), 0LL) == sum);
        for (ll x : q) assert(x >= 0);
    }
    // 木: 全形状
    for (auto shape : {TreeShape::mixed, TreeShape::random, TreeShape::path, TreeShape::star,
                       TreeShape::caterpillar, TreeShape::binary, TreeShape::deep}) {
        for (ll n = 1; n <= 40; ++n) {
            auto es = g.tree(n, {.shape = shape});
            assert(is_tree(n, es, 1) && !es.weighted);
        }
    }
    {
        auto es = g.tree(10, {.w = Range{5, 6}});
        assert(es.weighted);
        for (auto& e : es) assert(e.w == 5 || e.w == 6);
        int calls = 0;
        auto es2 = g.tree(10, {.w = [&] { return ++calls; }});
        assert(calls == 9 && es2.weighted);
        g.one_indexed = false;
        assert(is_tree(10, g.tree(10), 0));
        g.one_indexed = true;
    }
    // グラフ
    for (int k = 0; k < 300; ++k) {
        ll n = g.i(1, 12);
        bool directed = g.coin(), connected = g.coin();
        ll mx = directed ? n * (n - 1) : n * (n - 1) / 2;
        ll lo = connected ? n - 1 : 0;
        if (lo > mx) continue;
        ll m = g.i(lo, mx);
        auto es = g.graph(n, m, {.connected = connected, .directed = directed});
        assert((ll)es.size() == m);
        std::set<std::pair<ll, ll>> seen;
        std::vector<ll> p(n);
        std::iota(p.begin(), p.end(), 0);
        std::function<ll(ll)> f = [&](ll x) { return p[x] == x ? x : p[x] = f(p[x]); };
        for (auto& e : es) {
            assert(e.u != e.v && 1 <= e.u && e.u <= n && 1 <= e.v && e.v <= n);
            auto key = directed ? std::pair{e.u, e.v} : std::pair{std::min(e.u, e.v), std::max(e.u, e.v)};
            assert(seen.insert(key).second);
            p[f(e.u - 1)] = f(e.v - 1);
        }
        if (connected)
            for (ll v = 0; v < n; ++v) assert(f(v) == f(0));
    }
    {
        auto es = g.graph(5, 20, {.simple = false});
        assert(es.size() == 20);
    }
    // 文字列
    for (auto shape : {StrShape::mixed, StrShape::random, StrShape::periodic, StrShape::palindrome,
                       StrShape::fibonacci, StrShape::runs}) {
        for (ll n = 0; n <= 60; ++n) {
            auto s = g.str(n, "abc", {.shape = shape});
            assert((ll)s.size() == n);
            for (char c : s) assert(c == 'a' || c == 'b' || c == 'c');
            if (shape == StrShape::palindrome && n > 0) {
                (void)0;  // 回文の連結なので全体は回文とは限らない
            }
        }
    }
    {
        // 1 個だけの回文は回文
        int pal = 0;
        for (int k = 0; k < 200; ++k) {
            auto s = g.str(15, "ab", {.shape = StrShape::palindrome});
            pal += std::equal(s.begin(), s.end(), s.rbegin());
        }
        assert(pal > 50);
        auto f = g.str(13, "ab", {.shape = StrShape::fibonacci});
        assert(f.size() == 13);
        auto ss = g.strs(5, 20, "xy");
        ll tot = 0;
        for (auto& s : ss) tot += s.size(), assert(!s.empty());
        assert(ss.size() == 5 && tot <= 20);
        auto t = g.mutate("aaaa", 10, "b");
        assert(t.size() == 4);
        auto gr = g.grid(3, 4, ".#");
        assert(gr.size() == 3 && gr[0].size() == 4);
        auto s3 = g.str(5, [&] { return 'z'; });
        assert(s3 == "zzzzz");
    }
    // 出力
    {
        Gen o(0);
        o.out(1, 2LL, "x", 'c', std::vector<ll>{3, 4}, std::string("yz"));
        o.out(std::vector<int>{});
        Edges es;
        es.push_back({1, 2, 9});
        o.out(es);
        es.weighted = true;
        o.out(es);
        o.out_lines(std::vector<std::string>{"ab", "cd"});
        assert(o.buffer() == "1 2 x c 3 4 yz\n\n1 2\n1 2 9\nab\ncd\n");
    }
    std::puts("gen_test: all passed");
}
