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

// 頂点 s から（有向として）到達できる頂点数。
static ll reach_count(ll n, const Edges& es, ll s) {
    std::vector<std::vector<ll>> adj(n + 1);
    for (auto& e : es) adj[e.u].push_back(e.v);
    std::vector<char> seen(n + 1, 0);
    std::vector<ll> st{s};
    seen[s] = 1;
    ll c = 0;
    while (!st.empty()) {
        ll v = st.back();
        st.pop_back();
        ++c;
        for (ll w : adj[v])
            if (!seen[w]) seen[w] = 1, st.push_back(w);
    }
    return c;
}

// 本数・範囲・単純性・連結性を確かめ、弱連結成分の数を返す。
static ll check_edges(ll n, ll m, const Edges& es, bool directed, bool simple, bool connected) {
    assert((ll)es.size() == m);
    std::set<std::pair<ll, ll>> seen;
    std::vector<ll> p(n);
    std::iota(p.begin(), p.end(), 0);
    std::function<ll(ll)> f = [&](ll x) { return p[x] == x ? x : p[x] = f(p[x]); };
    for (auto& e : es) {
        assert(1 <= e.u && e.u <= n && 1 <= e.v && e.v <= n);
        if (simple) {
            assert(e.u != e.v);
            auto key = directed ? std::pair{e.u, e.v} : std::pair{std::min(e.u, e.v), std::max(e.u, e.v)};
            assert(seen.insert(key).second);
        }
        p[f(e.u - 1)] = f(e.v - 1);
    }
    ll comps = 0;
    for (ll v = 0; v < n; ++v) comps += f(v) == v;
    if (connected) assert(comps == 1);
    return comps;
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
    // sorted のオプション
    for (int k = 0; k < 200; ++k) {
        ll n = g.i(0, 30);
        auto a = g.sorted(n, 1, 5);
        assert(std::is_sorted(a.begin(), a.end()));
        auto b = g.sorted(n, 1, 5, {.desc = true});
        assert(std::is_sorted(b.rbegin(), b.rend()));
        auto c = g.sorted(n, -3, n + g.i(-3, 5), {.strict = true});
        for (ll i = 1; i < n; ++i) assert(c[i - 1] < c[i]);
        auto e = g.sorted(n, 0, 1000000000LL, {.strict = true, .desc = true});
        for (ll i = 1; i < n; ++i) assert(e[i - 1] > e[i]);
    }
    // seq: 全形状とタグ
    for (auto shape : {SeqShape::mixed, SeqShape::random, SeqShape::same, SeqShape::few, SeqShape::asc,
                       SeqShape::desc, SeqShape::edge}) {
        for (int k = 0; k < 100; ++k) {
            ll n = g.i(0, 20);
            auto a = g.seq(n, 3, 9, {.shape = shape});
            assert((ll)a.size() == n);
            for (ll x : a) assert(3 <= x && x <= 9);
            std::set<ll> st(a.begin(), a.end());
            if (shape == SeqShape::same) assert(st.size() <= 1);
            if (shape == SeqShape::few) assert(st.size() <= 3);
            if (shape == SeqShape::asc) assert(std::is_sorted(a.begin(), a.end()));
            if (shape == SeqShape::desc) assert(std::is_sorted(a.rbegin(), a.rend()));
            if (shape == SeqShape::edge) for (ll x : a) assert(x == 3 || x == 9);
        }
    }
    {
        Gen t(5);
        t.seq(10, Range{1, 2});
        assert(t.tags().size() == 1 && t.tags()[0].first.rfind("seq=", 0) == 0);
        Gen u(5);
        u.seq(10, 1, 2, {.shape = SeqShape::asc});
        assert(u.tags().empty());
    }
    // decimal
    {
        auto digits_after_point = [](const std::string& s) {
            auto p = s.find('.');
            return p == std::string::npos ? 0LL : (ll)(s.size() - p - 1);
        };
        std::set<ll> lens;
        for (int k = 0; k < 2000; ++k) {
            auto s = g.decimal(-1.5, 2.25, 3);
            double x = std::stod(s);
            assert(-1.5 - 1e-9 <= x && x <= 2.25 + 1e-9);
            ll len = digits_after_point(s);
            assert(len <= 3);
            if (len > 0) assert(s.back() != '0');
            assert(s != "-0");
            lens.insert(len);
            auto e = g.decimal(-1.5, 2.25, 3, {.exact = true});
            assert(digits_after_point(e) == 3);
            double y = std::stod(e);
            assert(-1.5 - 1e-9 <= y && y <= 2.25 + 1e-9);
        }
        assert(lens.size() == 4);  // 0..3 桁がすべて出る
        assert(g.decimal(0.1, 0.1, 1, {.exact = true}) == "0.1");
        assert(g.decimal(0.1, 0.1, 3, {.exact = true}) == "0.100");
        assert(g.decimal(-0.05, -0.05, 2) == "-0.05");
        assert(g.decimal(7, 7, 0, {.exact = true}) == "7");
        assert(g.decimal(0.11, 0.19, 2).size() == 4);  // 0.1 は範囲外なので 2 桁に上がる
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
        auto es = directed ? g.digraph(n, m, {.reach = connected ? Reach::weak : Reach::none})
                           : g.graph(n, m, {.connected = connected});
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
    // DAG
    for (int k = 0; k < 300; ++k) {
        ll n = g.i(1, 12);
        bool connected = g.coin(), root = connected && g.coin(), increasing = g.coin(), simple = g.coin(0.8);
        ll mx = n * (n - 1) / 2;
        ll lo = connected ? n - 1 : 0;
        ll m = simple ? g.i(lo, mx) : (n == 1 ? 0 : g.i(lo, mx + 5));
        auto es = g.dag(n, m, {.reach = root ? Reach::root : connected ? Reach::weak : Reach::none, .simple = simple,
                               .increasing = increasing});
        if (root) assert(reach_count(n, es, 1) == n);
        assert((ll)es.size() == m);
        std::set<std::pair<ll, ll>> seen;
        std::vector<std::vector<ll>> adj(n);
        std::vector<ll> indeg(n), p(n);
        std::iota(p.begin(), p.end(), 0);
        std::function<ll(ll)> f = [&](ll x) { return p[x] == x ? x : p[x] = f(p[x]); };
        for (auto& e : es) {
            assert(e.u != e.v && 1 <= e.u && e.u <= n && 1 <= e.v && e.v <= n);
            if (increasing) assert(e.u < e.v);
            if (simple) assert(seen.insert({e.u, e.v}).second);
            adj[e.u - 1].push_back(e.v - 1), ++indeg[e.v - 1];
            p[f(e.u - 1)] = f(e.v - 1);
        }
        std::vector<ll> q;
        for (ll v = 0; v < n; ++v)
            if (!indeg[v]) q.push_back(v);
        for (size_t h = 0; h < q.size(); ++h)
            for (ll w : adj[q[h]])
                if (!--indeg[w]) q.push_back(w);
        assert((ll)q.size() == n);  // 閉路なし
        if (connected)
            for (ll v = 0; v < n; ++v) assert(f(v) == f(0));
    }
    {
        auto es = g.dag(4, 3, {.w = Range{7, 7}});
        assert(es.weighted && es[0].w == 7);
    }
    // 二部グラフ（左右別番号 / 通し番号 / 総頂点数だけ）
    for (int k = 0; k < 600; ++k) {
        int mode = k % 3;
        ll n1 = g.i(0, 7), n2 = g.i(0, 7);
        if (mode == 2) n1 = g.i(1, 12), n2 = 0;
        if (n1 + n2 == 0) continue;
        ll n = n1 + n2;
        bool connected = g.coin(), simple = g.coin(0.8);
        ll mx = mode == 2 ? (n / 2) * (n - n / 2) : n1 * n2;
        if (connected && n >= 2 && mode != 2 && (n1 == 0 || n2 == 0)) continue;
        ll lo = connected ? n - 1 : 0;
        if (lo > mx) continue;
        ll m = simple || mx == 0 ? g.i(lo, mx) : g.i(lo, mx + 5);
        Edges es = mode == 2 ? g.bipartite(n, m, {.connected = connected, .simple = simple})
                             : g.bipartite(n1, n2, m, {.connected = connected, .simple = simple, .merged = mode == 1});
        assert((ll)es.size() == m);
        std::set<std::pair<ll, ll>> seen;
        std::vector<ll> p(n);
        std::iota(p.begin(), p.end(), 0);
        std::function<ll(ll)> f = [&](ll x) { return p[x] == x ? x : p[x] = f(p[x]); };
        std::vector<std::vector<ll>> adj(n);
        for (auto& e : es) {
            if (mode == 0) {
                assert(1 <= e.u && e.u <= n1 && 1 <= e.v && e.v <= n2);
                if (simple) assert(seen.insert({e.u, e.v}).second);
                p[f(e.u - 1)] = f(n1 + e.v - 1);
            } else {
                assert(e.u != e.v && 1 <= e.u && e.u <= n && 1 <= e.v && e.v <= n);
                if (simple) assert(seen.insert({std::min(e.u, e.v), std::max(e.u, e.v)}).second);
                adj[e.u - 1].push_back(e.v - 1), adj[e.v - 1].push_back(e.u - 1);
                p[f(e.u - 1)] = f(e.v - 1);
            }
        }
        if (mode != 0) {  // 2 彩色できる
            std::vector<int> col(n, -1);
            for (ll s = 0; s < n; ++s) {
                if (col[s] != -1) continue;
                col[s] = 0;
                std::vector<ll> st{s};
                while (!st.empty()) {
                    ll v = st.back();
                    st.pop_back();
                    for (ll w : adj[v]) {
                        if (col[w] == -1) col[w] = col[v] ^ 1, st.push_back(w);
                        assert(col[w] != col[v]);
                    }
                }
            }
        }
        if (connected)
            for (ll v = 0; v < n; ++v) assert(f(v) == f(0));
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
    // 根付き木の親の列
    for (int rep = 0; rep < 300; ++rep) {
        for (auto shape : {TreeShape::mixed, TreeShape::random, TreeShape::path, TreeShape::star,
                           TreeShape::caterpillar, TreeShape::binary, TreeShape::deep}) {
            const ll n = g.uniform(1, 30);
            auto p = g.parents(n, {.shape = shape});
            assert((ll)p.size() == n - 1);
            for (ll i = 2; i <= n; ++i) assert(1 <= p[i - 2] && p[i - 2] < i);
            for (ll base : {1, 0}) {
                g.one_indexed = base == 1;
                auto q = g.parents(n, {.shape = shape, .ordered = false});
                assert((ll)q.size() == n);
                ll roots = 0;
                for (ll v = 0; v < n; ++v) {
                    if (q[v] == base - 1) { ++roots; continue; }
                    assert(base <= q[v] && q[v] < n + base && q[v] - base != v);
                }
                assert(roots == 1);
                for (ll v = 0; v < n; ++v) {  // 根までたどれる（閉路なし）
                    ll x = v, steps = 0;
                    while (q[x] != base - 1) x = q[x] - base, assert(++steps <= n);
                }
                g.one_indexed = true;
            }
        }
    }
    {
        Gen a(7), b(7);
        assert(a.parents(50) == b.parents(50));
    }
    // 巨大整数
    {
        auto val_less = [](const std::string& x, const std::string& y, std::string_view d) {
            if (x.size() != y.size()) return x.size() < y.size();
            for (size_t k = 0; k < x.size(); ++k)
                if (x[k] != y[k]) return d.find(x[k]) < d.find(y[k]);
            return false;
        };
        for (int rep = 0; rep < 2000; ++rep) {
            auto s = g.big(g.uniform(1, 40));
            assert(s.size() == 1 || s[0] != '0');
            for (char c : s) assert('0' <= c && c <= '9');
            auto h = g.big(5, {.digits = digits::HEX, .lead_zero = true});
            for (char c : h) assert(digits::HEX.find(c) != std::string_view::npos);
        }
        bool seen_lead_zero = false, seen_short = false, seen_max = false, seen_small = false;
        const std::string max100 = "1" + std::string(100, '0');
        for (int rep = 0; rep < 5000; ++rep) {
            auto x = g.big_range("1", "1e100");
            assert(x[0] != '0' && !val_less(x, "1", digits::dec) && !val_less(max100, x, digits::dec));
            seen_short |= x.size() <= 3;
            seen_max |= x == max100;
            auto y = g.big(3, {.lead_zero = true});
            seen_lead_zero |= y[0] == '0';
            auto z = g.big_range("255", "4096", {.digits = digits::hex});
            assert(!val_less(z, "ff", digits::hex) && !val_less("1000", z, digits::hex));
            auto w = g.big_range("0", "1", {.digits = digits::bin});
            assert(w == "0" || w == "1");
            seen_small |= w == "0";
            auto v = g.big_range("123456789", "123456790");
            assert(v == "123456789" || v == "123456790");
            assert(g.big_range("0007", "7") == "7");
        }
        assert(seen_lead_zero && seen_short && seen_max && seen_small);
        // 範囲の書き方と進数の変換
        auto conv = [&](unsigned long long v, std::string_view d) {
            std::string r;
            do r += d[v % d.size()], v /= d.size();
            while (v > 0);
            return std::string(r.rbegin(), r.rend());
        };
        for (int rep = 0; rep < 2000; ++rep) {
            const unsigned long long v = g.uniform(0, LLONG_MAX) >> g.uniform(0, 62);
            const auto vs = std::to_string(v);
            for (auto d : {digits::bin, digits::oct, digits::dec, digits::hex, digits::HEX, digits::base36})
                assert(g.big_range(vs, vs, {.digits = d}) == conv(v, d));
        }
        assert(g.big_range("2^60-1", "2^60-1", {.digits = digits::bin}) == std::string(60, '1'));
        assert(g.big_range("2^60", "2^60") == "1152921504606846976");
        assert(g.big_range("1e18", "1e18", {.digits = digits::hex}) == "de0b6b3a7640000");
        assert(g.big_range("1e18-1", "1e18-1") == std::string(18, '9'));
        assert(g.big_range("3^40", "3^40", {.digits = digits::hex}) == conv(12157665459056928801ULL, digits::hex));
        assert(g.big_range("1e40", "1e40", {.digits = digits::hex}) == "1d6329f1c35ca4bfabb9f5610000000000");
        assert(g.big_range("0e5", "0") == "0");
        {
            for (int rep = 0; rep < 200; ++rep) {
                auto x = g.big_range("0", "16^200000-1", {.digits = digits::hex});
                assert(x.size() <= 200000 && (x.size() == 1 || x[0] != '0'));
            }
        }
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
    // 無向グラフの形: どの形でも本数・単純性・連結性を守る
    {
        const GraphShape shapes[] = {GraphShape::mixed, GraphShape::random, GraphShape::pieces, GraphShape::hub,
                                     GraphShape::path, GraphShape::bipartite};
        for (GraphShape sh : shapes)
            for (int k = 0; k < 300; ++k) {
                ll n = g.i(1, 12);
                bool connected = sh == GraphShape::pieces ? false : g.coin();
                bool simple = g.coin(0.8);
                ll mx = n * (n - 1) / 2;
                if (sh == GraphShape::bipartite) mx = (n / 2) * (n - n / 2);
                if (sh == GraphShape::pieces) {
                    if (n < 2) continue;
                    mx = (n - 1) * (n - 2) / 2;
                }
                if (!simple && n >= 2) mx += 5;
                ll lo = connected ? n - 1 : 0;
                if (lo > mx) continue;
                ll m = g.i(lo, mx);
                auto es = g.graph(n, m, {.shape = sh, .connected = connected, .simple = simple});
                ll comps = check_edges(n, m, es, false, simple, connected);
                if (sh == GraphShape::pieces) assert(comps >= 2);
            }
    }
    // 有向グラフの形と到達性
    {
        const DigraphShape shapes[] = {DigraphShape::mixed, DigraphShape::random, DigraphShape::pieces,
                                       DigraphShape::hub, DigraphShape::path, DigraphShape::dag};
        const Reach reaches[] = {Reach::none, Reach::weak, Reach::root, Reach::strong};
        for (DigraphShape sh : shapes)
            for (Reach r : reaches) {
                if (sh == DigraphShape::pieces && r != Reach::none) continue;
                if (sh == DigraphShape::dag && r == Reach::strong) continue;
                for (int k = 0; k < 200; ++k) {
                    ll n = g.i(1, 12);
                    bool simple = g.coin(0.8);
                    ll mx = n * (n - 1);
                    if (sh == DigraphShape::dag) mx = n * (n - 1) / 2;
                    if (sh == DigraphShape::pieces) {
                        if (n < 2) continue;
                        mx = (n - 1) * (n - 2);
                    }
                    if (!simple && n >= 2) mx += 5;
                    ll lo = r == Reach::none ? 0 : r == Reach::strong && n >= 2 ? n : n - 1;
                    if (lo > mx) continue;
                    ll m = g.i(lo, mx);
                    auto es = g.digraph(n, m, {.shape = sh, .reach = r, .simple = simple});
                    ll comps = check_edges(n, m, es, true, simple, r != Reach::none);
                    if (sh == DigraphShape::pieces) assert(comps >= 2);
                    if (r == Reach::root) assert(reach_count(n, es, 1) == n);
                    if (r == Reach::strong)
                        for (ll v = 1; v <= n; ++v) assert(reach_count(n, es, v) == n);
                }
            }
        // 0 始まりなら根は頂点 0
        Gen z(9);
        z.one_indexed = false;
        for (int k = 0; k < 50; ++k) {
            Edges es = z.digraph(8, 10, {.reach = Reach::root});
            for (auto& e : es) ++e.u, ++e.v;
            assert(reach_count(8, es, 1) == 8);
        }
    }
    // 大きくても密でも終わる
    {
        Gen big(7);
        assert((ll)big.graph(200000, 400000, {.shape = GraphShape::path}).size() == 400000);
        assert((ll)big.graph(200000, 400000, {.shape = GraphShape::pieces}).size() == 400000);
        assert((ll)big.graph(200000, 199999, {.shape = GraphShape::hub, .connected = true}).size() == 199999);
        assert((ll)big.graph(300, 300 * 299 / 2, {.shape = GraphShape::path}).size() == 300 * 299 / 2);
        assert((ll)big.graph(300, 299 * 298 / 2, {.shape = GraphShape::pieces}).size() == 299 * 298 / 2);
        assert((ll)big.digraph(200000, 400000, {.reach = Reach::strong}).size() == 400000);
        assert((ll)big.digraph(200000, 400000, {.shape = DigraphShape::path, .reach = Reach::root}).size() == 400000);
        assert((ll)big.digraph(300, 300 * 299, {.reach = Reach::strong}).size() == 300 * 299);
    }
    // タグ: mixed で選ばれた形と、g.tag で付けたもの
    {
        Gen t(3);
        t.str(10, "ab");
        t.str(10, "ab", {.shape = StrShape::runs});  // 形を指定したらタグは付かない
        t.tag("heavy");
        t.tag("heavy");
        auto& tags = t.tags();
        assert(tags.size() == 2);
        assert(tags[0].first.rfind("str=", 0) == 0 && tags[0].second == 1);
        assert(tags[1].first == "heavy" && tags[1].second == 2);
        Gen u(4);
        u.graph(10, 12);
        assert(u.tags().size() >= 1 && u.tags()[0].first.rfind("graph=", 0) == 0);
    }
    std::puts("gen_test: all passed");
}
