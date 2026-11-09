// gen.hpp の性質テスト: g++ -std=gnu++20 -I.. tests/gen_test.cpp
// 引数を付けると、名前にその文字列を含むテストだけ実行する（例: ./gen_test tree）。
#define STRESS_NO_MAIN
#include "stress/gen.hpp"
#include "tests/check.hpp"

#include <climits>
#include <map>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

using namespace stress;

// ---------- 判定の道具 ----------

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

// 頂点 s から（有向として）到達できる頂点数。頂点は 1..n。
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

// 本数・範囲・単純性・連結性を確かめ、弱連結成分の数を返す。頂点が範囲外なら -1。
static ll check_edges(ll n, ll m, const Edges& es, bool directed, bool simple, bool connected) {
    CHECK((ll)es.size() == m);
    std::set<std::pair<ll, ll>> seen;
    std::vector<ll> p(n);
    std::iota(p.begin(), p.end(), 0);
    std::function<ll(ll)> f = [&](ll x) { return p[x] == x ? x : p[x] = f(p[x]); };
    for (auto& e : es) {
        CHECK_OR(1 <= e.u && e.u <= n && 1 <= e.v && e.v <= n, return -1);
        if (simple) {
            CHECK(e.u != e.v);
            auto key = directed ? std::pair{e.u, e.v} : std::pair{std::min(e.u, e.v), std::max(e.u, e.v)};
            CHECK(seen.insert(key).second);
        }
        p[f(e.u - 1)] = f(e.v - 1);
    }
    ll comps = 0;
    for (ll v = 0; v < n; ++v) comps += f(v) == v;
    if (connected) CHECK(comps == 1);
    return comps;
}

// 向きを無視した隣接リスト（0 始まり、自己ループと多重辺は除く）。
static std::vector<std::vector<ll>> neighbors(ll n, const Edges& es, ll base = 1) {
    std::vector<std::set<ll>> s(n);
    for (auto& e : es)
        if (e.u != e.v) s[e.u - base].insert(e.v - base), s[e.v - base].insert(e.u - base);
    std::vector<std::vector<ll>> adj(n);
    for (ll v = 0; v < n; ++v) adj[v].assign(s[v].begin(), s[v].end());
    return adj;
}

static std::vector<ll> bfs(const std::vector<std::vector<ll>>& adj, ll s) {
    std::vector<ll> d(adj.size(), -1), q{s};
    d[s] = 0;
    for (size_t h = 0; h < q.size(); ++h)
        for (ll w : adj[q[h]])
            if (d[w] < 0) d[w] = d[q[h]] + 1, q.push_back(w);
    return d;
}

// 向きを無視した直径（連結成分の中での最大距離）。
static ll diameter(const std::vector<std::vector<ll>>& adj) {
    ll best = 0;
    for (ll s = 0; s < (ll)adj.size(); ++s)
        for (ll x : bfs(adj, s)) best = std::max(best, x);
    return best;
}
// 木の直径（2 回の BFS）。
static ll tree_diameter(const std::vector<std::vector<ll>>& adj) {
    if (adj.empty()) return 0;
    auto d = bfs(adj, 0);
    ll far = std::max_element(d.begin(), d.end()) - d.begin();
    auto d2 = bfs(adj, far);
    return *std::max_element(d2.begin(), d2.end());
}

static ll max_degree(const std::vector<std::vector<ll>>& adj) {
    ll d = 0;
    for (auto& a : adj) d = std::max<ll>(d, a.size());
    return d;
}

static ll floor_log2(ll n) {
    ll k = 0;
    while ((2LL << k) <= n) ++k;
    return k;
}

// 木（頂点 0..n-1 の隣接リスト）が形 shape の性質を満たすか。どれも「その形なら必ず成り立つ」条件。
//   path: 最大次数 2 以下
//   star: 次数 n-1 の頂点がある
//   caterpillar: 葉を除くとパス（次数 2 以上の頂点だけで最大次数 2 以下）
//   binary: 最大次数 3 以下で、直径が 2 floor(log2 n) 以下（ヒープの形）
//   deep: 直径が ceil((n-1)/3) 以上（各頂点の親は 3 つ前までの番号）
static bool tree_has_shape(const std::vector<std::vector<ll>>& adj, std::string_view shape) {
    const ll n = adj.size();
    if (shape == "path") return max_degree(adj) <= 2;
    if (shape == "star") return n <= 2 || max_degree(adj) == n - 1;
    if (shape == "caterpillar") {
        if (n <= 2) return true;
        for (ll v = 0; v < n; ++v) {
            if (adj[v].size() < 2) continue;
            ll inner = 0;
            for (ll w : adj[v]) inner += adj[w].size() >= 2;
            if (inner > 2) return false;
        }
        return true;
    }
    if (shape == "binary") return max_degree(adj) <= 3 && tree_diameter(adj) <= 2 * floor_log2(n);
    if (shape == "deep") return tree_diameter(adj) >= (n - 1 + 2) / 3;
    return shape == "random";
}

// 頂点を 3 つまで選んで、それに触れる辺の数の最大（n は小さい前提）。
static ll best_hub_cover(ll n, const Edges& es) {
    std::vector<ll> deg(n);
    std::map<std::pair<ll, ll>, ll> between;
    for (auto& e : es) {
        ll u = e.u - 1, v = e.v - 1;
        ++deg[u];
        if (u != v) ++deg[v], ++between[{std::min(u, v), std::max(u, v)}];
    }
    auto bt = [&](ll a, ll b) {
        auto it = between.find({std::min(a, b), std::max(a, b)});
        return it == between.end() ? 0 : it->second;
    };
    ll best = 0;
    for (ll a = 0; a < n; ++a)
        for (ll b = a + 1; b < n; ++b)
            for (ll c = b + 1; c < n; ++c)
                best = std::max(best, deg[a] + deg[b] + deg[c] - bt(a, b) - bt(a, c) - bt(b, c));
    return best;
}

static bool two_colorable(const std::vector<std::vector<ll>>& adj) {
    const ll n = adj.size();
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
                if (col[w] == col[v]) return false;
            }
        }
    }
    return true;
}

static bool acyclic(ll n, const Edges& es) {
    std::vector<std::vector<ll>> adj(n);
    std::vector<ll> indeg(n);
    for (auto& e : es) adj[e.u - 1].push_back(e.v - 1), ++indeg[e.v - 1];
    std::vector<ll> q;
    for (ll v = 0; v < n; ++v)
        if (!indeg[v]) q.push_back(v);
    for (size_t h = 0; h < q.size(); ++h)
        for (ll w : adj[q[h]])
            if (!--indeg[w]) q.push_back(w);
    return (ll)q.size() == n;
}

// graph / digraph の形の性質。頂点は 1..n。connected は土台を張ったかどうか。
//   pieces: 弱連結成分が 2 つ以上
//   bipartite: 2 彩色できる。dag: 閉路なし
//   path: 番号の差が w 以下の頂点どうしだけを結ぶので、隣接頂点数は 2w 以下。
//         連結なら直径は (n-1)/w 以上。w は 1〜3 で、単純グラフで m 本入らないときだけ広がる
//   hub: 辺の 8 割が 1〜3 個の中心につながる。疎なときだけ、3 頂点で辺の 6 割以上を覆うかを見る。
//        strong なら中心との往復 2(n-1) 本を土台の本数とみなす
static bool graph_has_shape(ll n, ll m, const Edges& es, std::string_view shape, bool directed, bool simple,
                            bool connected, bool strong = false) {
    auto adj = neighbors(n, es);
    if (shape == "pieces") {
        ll comps = 0;
        std::vector<char> seen(n, 0);
        for (ll v = 0; v < n; ++v) {
            if (seen[v]) continue;
            ++comps;
            auto d = bfs(adj, v);
            for (ll u = 0; u < n; ++u)
                if (d[u] >= 0) seen[u] = 1;
        }
        return comps >= 2;
    }
    if (shape == "bipartite") return two_colorable(adj);
    if (shape == "dag") return acyclic(n, es);
    if (shape == "path") {
        auto capacity = [&](ll w) { return (w * n - w * (w + 1) / 2) * (directed ? 2 : 1); };
        ll w = std::min<ll>(3, std::max<ll>(n - 1, 1));
        if (simple)
            while (w < n - 1 && capacity(w) < m) ++w;
        if (max_degree(adj) > 2 * w) return false;
        if (connected && n >= 2 && diameter(adj) < (n - 1 + w - 1) / w) return false;
        return true;
    }
    if (shape == "hub") {
        const ll base = strong ? 2 * (n - 1) : connected ? n - 1 : 0;
        if (m < base) return true;  // strong で往復が入らないときは、0 を通る長い閉路が土台になる
        const bool sparse = n >= 40 && m - base <= n / 2 && (connected || m >= 20);
        if (!sparse) return true;
        return best_hub_cover(n, es) * 10 >= m * 6;
    }
    return shape == "random";
}

static bool is_palindrome(std::string_view s) { return std::equal(s.begin(), s.end(), s.rbegin()); }

// 文字列の形の性質。
//   periodic: ある周期 p で s[k] != s[k-p] となる位置が 4 個以下（作ったあとで 0〜2 文字書き換える）
//   palindrome: 回文 4 個以下の連結
//   fibonacci: 2 種類の文字を x, y にするとフィボナッチ文字列 xyxxyxyx... の部分文字列
//   runs: n >= 60 なら連の個数が n/3 以下（連の長さは n/4 程度まで）
static bool str_has_shape(const std::string& s, std::string_view shape) {
    const ll n = s.size();
    if (n == 0) return true;
    if (shape == "periodic") {
        const ll pmax = std::max<ll>({1, std::min<ll>(n, 4), n / 2});
        for (ll p = 1; p <= std::min(pmax, n); ++p) {
            ll bad = 0;
            for (ll k = p; k < n; ++k) bad += s[k] != s[k - p];
            if (bad <= 4) return true;
        }
        return false;
    }
    if (shape == "palindrome") {
        std::vector<ll> dp(n + 1, LLONG_MAX / 2);  // dp[k]: 先頭 k 文字を回文に分ける最小個数
        dp[0] = 0;
        for (ll k = 1; k <= n; ++k)
            for (ll j = 0; j < k; ++j)
                if (is_palindrome(std::string_view(s).substr(j, k - j))) dp[k] = std::min(dp[k], dp[j] + 1);
        return dp[n] <= 4;
    }
    if (shape == "fibonacci") {
        std::set<char> cs(s.begin(), s.end());
        if (cs.size() > 2) return false;
        std::string f0 = "x", f1 = "xy";
        while ((ll)f1.size() < 2 * n + 2) {
            std::string f2 = f1 + f0;
            f0 = std::move(f1);
            f1 = std::move(f2);
        }
        std::vector<char> v(cs.begin(), cs.end());
        for (int flip = 0; flip < 2; ++flip) {
            std::string t = s;
            for (char& c : t) c = (c == v[0]) != (flip == 1) ? 'x' : 'y';
            if (f1.find(t) != std::string::npos) return true;
        }
        return false;
    }
    if (shape == "runs") {
        if (n < 60) return true;
        ll runs = 1;
        for (ll k = 1; k < n; ++k) runs += s[k] != s[k - 1];
        return runs <= n / 3;
    }
    return shape == "random";
}

static bool seq_has_shape(const std::vector<ll>& a, ll lo, ll hi, std::string_view shape) {
    std::set<ll> st(a.begin(), a.end());
    for (ll x : a)
        if (x < lo || hi < x) return false;
    if (shape == "same") return st.size() <= 1;
    if (shape == "few") return st.size() <= 3;
    if (shape == "asc") return std::is_sorted(a.begin(), a.end());
    if (shape == "desc") return std::is_sorted(a.rbegin(), a.rend());
    if (shape == "edge") {
        for (ll x : a)
            if (x != lo && x != hi) return false;
        return true;
    }
    return shape == "random";
}

// "prefix=形" のタグの形の部分。無ければ空。
static std::string shape_tag(const Gen& g, std::string_view prefix) {
    for (auto& [t, c] : g.tags())
        if (t.size() > prefix.size() && t.compare(0, prefix.size(), prefix) == 0 && t[prefix.size()] == '=')
            return t.substr(prefix.size() + 1);
    return "";
}

// f() が gen_error（終了コード 3）で止まるか。子プロセスで実行し、エラー表示は捨てる。
template <class F>
static bool dies_with_gen_error(F f) {
    std::fflush(stdout);
    std::fflush(stderr);
    const pid_t pid = fork();
    if (pid == 0) {
        const int fd = open("/dev/null", O_WRONLY);
        dup2(fd, 2);
        f();
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 3;
}

static Edges parents_to_edges(const std::vector<ll>& p, bool ordered, ll base) {
    Edges es;
    if (ordered) {
        for (ll i = 0; i < (ll)p.size(); ++i) es.push_back({i + 1 + base, p[i], 0});  // 頂点 i+2 の親
    } else {
        for (ll v = 0; v < (ll)p.size(); ++v)
            if (p[v] != base - 1) es.push_back({v + base, p[v], 0});
    }
    return es;
}

// ---------- 乱数の基本 ----------

static void test_seed() {  // 同じ seed なら同じ出力
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        Gen a(seed), b(seed);
        for (int k = 0; k < 100; ++k) CHECK(a.i(-5, 1000000) == b.i(-5, 1000000));
        CHECK(a.str(30, "ab") == b.str(30, "ab"));
        CHECK(a.tree(20, {.w = Range{1, 9}}).size() == 19 && a.i(0, 1 << 30) == (b.tree(20, {.w = Range{1, 9}}), b.i(0, 1 << 30)));
        CHECK(a.parents(50) == b.parents(50));
    }
}

static void test_range() {
    Gen g(1);
    for (int k = 0; k < 100000; ++k) {
        ll x = g.i(-3, 7);
        CHECK(-3 <= x && x <= 7);
        (void)g.uniform(LLONG_MIN, LLONG_MAX);
    }
    // 端が出やすい
    int hit = 0;
    for (int k = 0; k < 10000; ++k) hit += g.i(0, 1000000000) == 1000000000;
    CHECK(hit > 100);
}

static void test_size_cap() {
    Gen h(1, 3);
    for (int k = 0; k < 1000; ++k) {
        ll n = h.size(1, 100);
        CHECK(1 <= n && n <= 3);
    }
    Gen h2(1, 0);
    CHECK(h2.size(2, 100) == 2);  // lo は下回らない
}

static void test_decimal() {
    Gen g(2);
    auto digits_after_point = [](const std::string& s) {
        auto p = s.find('.');
        return p == std::string::npos ? 0LL : (ll)(s.size() - p - 1);
    };
    std::set<ll> lens;
    for (int k = 0; k < 2000; ++k) {
        auto s = g.decimal(-1.5, 2.25, 3);
        double x = std::stod(s);
        CHECK(-1.5 - 1e-9 <= x && x <= 2.25 + 1e-9);
        ll len = digits_after_point(s);
        CHECK(len <= 3);
        if (len > 0) CHECK(s.back() != '0');
        CHECK(s != "-0");
        lens.insert(len);
        auto e = g.decimal(-1.5, 2.25, 3, {.exact = true});
        CHECK(digits_after_point(e) == 3);
        double y = std::stod(e);
        CHECK(-1.5 - 1e-9 <= y && y <= 2.25 + 1e-9);
    }
    CHECK(lens.size() == 4);  // 0..3 桁がすべて出る
    CHECK(g.decimal(0.1, 0.1, 1, {.exact = true}) == "0.1");
    CHECK(g.decimal(0.1, 0.1, 3, {.exact = true}) == "0.100");
    CHECK(g.decimal(-0.05, -0.05, 2) == "-0.05");
    CHECK(g.decimal(7, 7, 0, {.exact = true}) == "7");
    CHECK(g.decimal(0.11, 0.19, 2).size() == 4);  // 0.1 は範囲外なので 2 桁に上がる
}

// ---------- 配列 ----------

static void test_distinct_perm_partition() {
    Gen g(3);
    for (int k = 0; k < 200; ++k) {
        ll n = g.i(0, 50);
        auto d = g.distinct(n, 10, 10 + n + g.i(0, 3));
        CHECK((ll)std::set<ll>(d.begin(), d.end()).size() == n);
        auto d2 = g.distinct(n, 0, 1000000000000LL);
        CHECK((ll)std::set<ll>(d2.begin(), d2.end()).size() == n);
        auto p = g.perm(n);
        std::sort(p.begin(), p.end());
        for (ll i = 0; i < n; ++i) CHECK(p[i] == i + 1);
        ll sum = g.i(n, 100), parts = g.i(1, 10);
        auto q = g.partition(sum, parts);
        CHECK((ll)q.size() == parts);
        CHECK(std::accumulate(q.begin(), q.end(), 0LL) == sum);
        for (ll x : q) CHECK(x >= 0);
        auto r = g.partition(sum + 3 * parts, parts, 3);
        for (ll x : r) CHECK(x >= 3);
    }
}

static void test_sorted() {
    Gen g(4);
    for (int k = 0; k < 200; ++k) {
        ll n = g.i(0, 30);
        auto a = g.sorted(n, 1, 5);
        CHECK(std::is_sorted(a.begin(), a.end()));
        auto b = g.sorted(n, 1, 5, {.desc = true});
        CHECK(std::is_sorted(b.rbegin(), b.rend()));
        auto c = g.sorted(n, -3, n + g.i(-3, 5), {.strict = true});
        for (ll i = 1; i < n; ++i) CHECK(c[i - 1] < c[i]);
        auto e = g.sorted(n, 0, 1000000000LL, {.strict = true, .desc = true});
        for (ll i = 1; i < n; ++i) CHECK(e[i - 1] > e[i]);
    }
}

static void test_seq_shapes() {
    Gen g(5);
    const std::pair<SeqShape, const char*> shapes[] = {{SeqShape::random, "random"}, {SeqShape::same, "same"},
                                                       {SeqShape::few, "few"},       {SeqShape::asc, "asc"},
                                                       {SeqShape::desc, "desc"},     {SeqShape::edge, "edge"}};
    for (auto [shape, name] : shapes)
        for (int k = 0; k < 100; ++k) {
            ll n = g.i(0, 20);
            auto a = g.seq(n, 3, 9, {.shape = shape});
            CHECK((ll)a.size() == n);
            CHECK(seq_has_shape(a, 3, 9, name));
        }
    CHECK(g.tags().empty());  // 形を指定したらタグは付かない
}

static void test_seq_mixed() {
    std::set<std::string> seen;
    for (std::uint64_t seed = 0; seed < 300; ++seed) {
        Gen t(seed);
        auto a = t.seq(t.i(0, 20), Range{3, 9});
        auto s = shape_tag(t, "seq");
        CHECK(t.tags().size() == 1);
        CHECK(seq_has_shape(a, 3, 9, s));
        seen.insert(s);
    }
    CHECK(seen.size() == 6);  // 全部の形が出る
}

// ---------- 木 ----------

static const std::pair<TreeShape, const char*> kTreeShapes[] = {
    {TreeShape::random, "random"}, {TreeShape::path, "path"},     {TreeShape::star, "star"},
    {TreeShape::caterpillar, "caterpillar"}, {TreeShape::binary, "binary"}, {TreeShape::deep, "deep"}};

static void test_tree_shapes() {
    Gen g(6);
    for (auto [shape, name] : kTreeShapes) {
        for (ll n = 1; n <= 40; ++n) {
            auto es = g.tree(n, {.shape = shape});
            CHECK_OR(is_tree(n, es, 1), continue);
            CHECK(!es.weighted);
            CHECK(tree_has_shape(neighbors(n, es), name));
        }
        // 大きい木でも形を守る（deep の直径、binary の高さなどは n が大きいほど他の形と違う）
        for (int k = 0; k < 3; ++k) {
            auto es = g.tree(1000, {.shape = shape});
            CHECK_OR(is_tree(1000, es, 1), continue);
            CHECK(tree_has_shape(neighbors(1000, es), name));
        }
    }
    CHECK(g.tags().empty());
    // 判定そのものの確認: 違う形は弾く
    Gen h(60);
    CHECK(!tree_has_shape(neighbors(1000, h.tree(1000, {.shape = TreeShape::random})), "deep"));
    CHECK(!tree_has_shape(neighbors(1000, h.tree(1000, {.shape = TreeShape::random})), "path"));
    CHECK(!tree_has_shape(neighbors(1000, h.tree(1000, {.shape = TreeShape::deep})), "binary"));
}

static void test_tree_mixed() {
    std::set<std::string> seen;
    for (std::uint64_t seed = 0; seed < 300; ++seed) {
        Gen t(seed);
        const ll n = t.i(1, 60);
        auto es = t.tree(n);
        auto s = shape_tag(t, "tree");
        CHECK(t.tags().size() == 1);
        CHECK_OR(is_tree(n, es, 1), continue);
        CHECK(tree_has_shape(neighbors(n, es), s));
        seen.insert(s);
    }
    CHECK(seen.size() == 6);
}

static void test_tree_options() {
    Gen g(7);
    auto es = g.tree(10, {.w = Range{5, 6}});
    CHECK(es.weighted);
    for (auto& e : es) CHECK(e.w == 5 || e.w == 6);
    int calls = 0;
    auto es2 = g.tree(10, {.w = [&] { return ++calls; }});
    CHECK(calls == 9 && es2.weighted);
    g.one_indexed = false;
    CHECK(is_tree(10, g.tree(10), 0));
    for (int k = 0; k < 50; ++k) {
        ll n = g.i(1, 12);
        auto es3 = g.tree(n, {.ordered = true});
        CHECK(is_tree(n, es3, 0));
        for (auto& e : es3) CHECK(e.u < e.v);
    }
}

// one_indexed = false なら、同じ seed で 1 始まりのときの番号からちょうど 1 引いたものになる
static void test_one_indexed() {
    auto shifted = [](const Edges& a, const Edges& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].u != b[i].u + 1 || a[i].v != b[i].v + 1 || a[i].w != b[i].w) return false;
        return true;
    };
    auto shifted_vec = [](const std::vector<ll>& a, const std::vector<ll>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i] + 1) return false;
        return true;
    };
    // f(g) を 1 始まりと 0 始まりで同じ seed から呼ぶ
    auto both = [](std::uint64_t seed, auto f) {
        Gen one(seed), zero(seed);
        zero.one_indexed = false;
        return std::pair{f(one), f(zero)};
    };
    Gen r(30);
    for (int k = 0; k < 200; ++k) {
        const ll n = r.i(2, 12), m = r.i(n - 1, n * (n - 1) / 2);
        const ll n1 = r.i(1, 6), n2 = r.i(1, 6), mb = r.i(0, n1 * n2);
        const ll half = (n / 2) * (n - n / 2);
        const std::uint64_t s = 1000 + k;
        auto [t1, t0] = both(s, [&](Gen& g) { return g.tree(n, {.w = Range{1, 9}}); });
        CHECK(shifted(t1, t0) && is_tree(n, t0, 0));
        auto [o1, o0] = both(s, [&](Gen& g) { return g.tree(n, {.ordered = true}); });
        CHECK(shifted(o1, o0));
        auto [p1, p0] = both(s, [&](Gen& g) { return g.parents(n); });
        CHECK(shifted_vec(p1, p0));
        auto [q1, q0] = both(s, [&](Gen& g) { return g.parents(n, {.ordered = false}); });
        CHECK(shifted_vec(q1, q0));  // 根の親も 0 から -1 になる
        auto [g1, g0] = both(s, [&](Gen& g) { return g.graph(n, m, {.connected = true}); });
        CHECK(shifted(g1, g0));
        auto [d1, d0] = both(s, [&](Gen& g) { return g.digraph(n, m); });
        CHECK(shifted(d1, d0));
        auto [dr1, dr0] = both(s, [&](Gen& g) { return g.digraph(n, m, {.reach = Reach::root}); });
        CHECK(shifted(dr1, dr0));
        auto [ds1, ds0] = both(s, [&](Gen& g) { return g.digraph(n, std::max(m, n), {.reach = Reach::strong}); });
        CHECK(shifted(ds1, ds0));
        auto [a1, a0] = both(s, [&](Gen& g) { return g.dag(n, m); });
        CHECK(shifted(a1, a0));
        auto [b1, b0] = both(s, [&](Gen& g) { return g.bipartite(n1, n2, mb); });
        CHECK(shifted(b1, b0));
        auto [bo1, bo0] = both(s, [&](Gen& g) { return g.bipartite(n1, n2, mb, {.label = BipartiteLabel::offset}); });
        CHECK(shifted(bo1, bo0));
        auto [bm1, bm0] = both(s, [&](Gen& g) { return g.bipartite(n, std::min(m, half)); });
        CHECK(shifted(bm1, bm0));
        auto [v1, v0] = both(s, [&](Gen& g) { return g.perm(n); });
        CHECK(shifted_vec(v1, v0));
        // 範囲も直接見る
        for (auto* es : {&t0, &g0, &d0, &dr0, &ds0, &a0, &bm0})
            for (auto& e : *es) CHECK(0 <= std::min(e.u, e.v) && std::max(e.u, e.v) < n);
        for (auto& e : b0) CHECK(0 <= e.u && e.u < n1 && 0 <= e.v && e.v < n2);
        for (auto& e : bo0) CHECK(0 <= e.u && e.u < n1 && n1 <= e.v && e.v < n1 + n2);
    }
    // 途中で切り替えると、それ以降の呼び出しだけ変わる（前に作ったものはそのまま）
    {
        Gen g(31);
        auto before = g.perm(5);
        g.one_indexed = false;
        auto after = g.perm(5);
        g.one_indexed = true;
        auto again = g.perm(5);
        auto sorted = [](std::vector<ll> v) { std::sort(v.begin(), v.end()); return v; };
        CHECK(sorted(before) == std::vector<ll>({1, 2, 3, 4, 5}));
        CHECK(sorted(after) == std::vector<ll>({0, 1, 2, 3, 4}));
        CHECK(sorted(again) == std::vector<ll>({1, 2, 3, 4, 5}));
    }
}

// 根付き木の親の列
static void test_parents() {
    Gen g(8);
    for (int rep = 0; rep < 100; ++rep)
        for (auto [shape, name] : kTreeShapes) {
            const ll n = g.uniform(1, 30);
            auto p = g.parents(n, {.shape = shape});
            CHECK_OR((ll)p.size() == n - 1, continue);
            bool ok = true;
            for (ll i = 2; i <= n; ++i) ok &= 1 <= p[i - 2] && p[i - 2] < i;
            CHECK_OR(ok, continue);
            CHECK(tree_has_shape(neighbors(n, parents_to_edges(p, true, 1)), name));
            for (ll base : {1, 0}) {
                g.one_indexed = base == 1;
                auto q = g.parents(n, {.shape = shape, .ordered = false});
                g.one_indexed = true;
                CHECK_OR((ll)q.size() == n, continue);
                ll roots = 0;
                bool in_range = true;
                for (ll v = 0; v < n; ++v) {
                    if (q[v] == base - 1) {
                        ++roots;
                        continue;
                    }
                    in_range &= base <= q[v] && q[v] < n + base && q[v] - base != v;
                }
                CHECK_OR(in_range && roots == 1, continue);
                for (ll v = 0; v < n; ++v) {  // 根までたどれる（閉路なし）
                    ll x = v, steps = 0;
                    while (q[x] != base - 1 && steps <= n) x = q[x] - base, ++steps;
                    CHECK(steps <= n);
                }
                CHECK(tree_has_shape(neighbors(n, parents_to_edges(q, false, base), base), name));
            }
        }
}

static void test_parents_mixed() {
    std::set<std::string> seen;
    for (std::uint64_t seed = 0; seed < 300; ++seed) {
        Gen t(seed);
        const ll n = t.i(1, 60);
        auto p = t.parents(n);
        auto s = shape_tag(t, "parents");
        CHECK(t.tags().size() == 1);
        CHECK(tree_has_shape(neighbors(n, parents_to_edges(p, true, 1)), s));
        seen.insert(s);
    }
    CHECK(seen.size() == 6);
}

// ---------- グラフ ----------

static void test_graph_basic() {
    Gen g(9);
    for (int k = 0; k < 300; ++k) {
        ll n = g.i(1, 12);
        bool directed = g.coin(), connected = g.coin();
        ll mx = directed ? n * (n - 1) : n * (n - 1) / 2;
        ll lo = connected ? n - 1 : 0;
        if (lo > mx) continue;
        ll m = g.i(lo, mx);
        auto es = directed ? g.digraph(n, m, {.reach = connected ? Reach::weak : Reach::none})
                           : g.graph(n, m, {.connected = connected});
        check_edges(n, m, es, directed, true, connected);
    }
    CHECK(g.graph(5, 20, {.simple = false}).size() == 20);
}

static const std::pair<GraphShape, const char*> kGraphShapes[] = {{GraphShape::random, "random"},
                                                                  {GraphShape::bipartite, "bipartite"},
                                                                  {GraphShape::pieces, "pieces"},
                                                                  {GraphShape::hub, "hub"},
                                                                  {GraphShape::path, "path"}};

// 形を指定したときの m の上限（単純グラフ）。
static ll graph_max_edges(GraphShape sh, ll n) {
    if (sh == GraphShape::bipartite) return (n / 2) * (n - n / 2);
    if (sh == GraphShape::pieces) return (n - 1) * (n - 2) / 2;
    return n * (n - 1) / 2;
}

// 無向グラフの形: どの形でも本数・単純性・連結性を守り、形の性質を満たす
static void test_graph_shapes() {
    Gen g(10);
    for (auto [sh, name] : kGraphShapes)
        for (int k = 0; k < 400; ++k) {
            // 半分は hub の判定が効く疎で大きいグラフ
            const bool big = k % 2;
            ll n = big ? g.i(40, 60) : g.i(1, 12);
            bool connected = sh == GraphShape::pieces ? false : g.coin();
            bool simple = g.coin(0.8);
            if (sh == GraphShape::pieces && n < 2) continue;
            ll mx = graph_max_edges(sh, n);
            if (!simple && n >= 2) mx += 5;
            ll lo = connected ? n - 1 : 0;
            if (big) mx = std::min(mx, lo + n / 2);
            if (lo > mx) continue;
            ll m = g.i(lo, mx);
            bool ordered = g.coin();
            auto es = g.graph(n, m, {.shape = sh, .connected = connected, .simple = simple, .ordered = ordered});
            CHECK_OR(check_edges(n, m, es, false, simple, connected) >= 0, continue);
            if (ordered)
                for (auto& e : es) CHECK(e.u <= e.v && (simple ? e.u < e.v : true));
            CHECK(graph_has_shape(n, m, es, name, false, simple, connected));
        }
    CHECK(shape_tag(g, "graph").empty());  // 形を指定したら形のタグは付かない
    // 判定そのものの確認: 素のランダムは path / hub / pieces の判定を通らない
    Gen h(61);
    CHECK(!graph_has_shape(60, 89, h.graph(60, 89, {.shape = GraphShape::random, .connected = true}), "path", false,
                           true, true));
    CHECK(!graph_has_shape(60, 30, h.graph(60, 30, {.shape = GraphShape::random}), "hub", false, true, false));
    CHECK(!graph_has_shape(60, 89, h.graph(60, 89, {.shape = GraphShape::random, .connected = true}), "pieces",
                           false, true, true));
}

static void test_graph_mixed() {
    std::set<std::string> seen;
    for (std::uint64_t seed = 0; seed < 600; ++seed) {
        Gen t(seed);
        const bool big = seed % 2;
        ll n = big ? t.i(40, 60) : t.i(2, 12);
        bool connected = t.coin(), simple = t.coin(0.8);
        ll lo = connected ? n - 1 : 0, mx = n * (n - 1) / 2;
        if (big) mx = lo + n / 2;
        ll m = t.i(lo, mx);
        auto es = t.graph(n, m, {.connected = connected, .simple = simple});
        auto s = shape_tag(t, "graph");
        CHECK_OR(!s.empty(), continue);
        CHECK_OR(check_edges(n, m, es, false, simple, connected) >= 0, continue);
        CHECK(graph_has_shape(n, m, es, s, false, simple, connected));
        seen.insert(s);
    }
    CHECK(seen.size() == 5);
}

static const std::pair<DigraphShape, const char*> kDigraphShapes[] = {{DigraphShape::random, "random"},
                                                                      {DigraphShape::dag, "dag"},
                                                                      {DigraphShape::pieces, "pieces"},
                                                                      {DigraphShape::hub, "hub"},
                                                                      {DigraphShape::path, "path"}};

// 有向グラフの形と到達性。strong の形は digraph_strong_shapes で見る
static void test_digraph_shapes() {
    Gen g(11);
    const Reach reaches[] = {Reach::none, Reach::weak, Reach::root, Reach::strong};
    for (auto [sh, name] : kDigraphShapes)
        for (Reach r : reaches) {
            if (sh == DigraphShape::pieces && r != Reach::none) continue;
            if (sh == DigraphShape::dag && r == Reach::strong) continue;
            for (int k = 0; k < 200; ++k) {
                const bool big = k % 2;
                ll n = big ? g.i(40, 60) : g.i(1, 12);
                bool simple = g.coin(0.8);
                ll mx = n * (n - 1);
                if (sh == DigraphShape::dag) mx = n * (n - 1) / 2;
                if (sh == DigraphShape::pieces) {
                    if (n < 2) continue;
                    mx = (n - 1) * (n - 2);
                }
                if (!simple && n >= 2) mx += 5;
                ll lo = r == Reach::none ? 0 : r == Reach::strong && n >= 2 ? n : n - 1;
                if (big) mx = std::min(mx, lo + n / 2);
                if (lo > mx) continue;
                ll m = g.i(lo, mx);
                auto es = g.digraph(n, m, {.shape = sh, .reach = r, .simple = simple});
                CHECK_OR(check_edges(n, m, es, true, simple, r != Reach::none) >= 0, continue);
                if (r == Reach::root) CHECK(reach_count(n, es, 1) == n);
                if (r == Reach::strong)
                    for (ll v = 1; v <= n; ++v) CHECK(reach_count(n, es, v) == n);
                if (r != Reach::strong) CHECK(graph_has_shape(n, m, es, name, true, simple, r != Reach::none));
            }
        }
    // 0 始まりなら根は頂点 0
    Gen z(9);
    z.one_indexed = false;
    for (int k = 0; k < 50; ++k) {
        Edges es = z.digraph(8, 10, {.reach = Reach::root});
        for (auto& e : es) ++e.u, ++e.v;
        CHECK(reach_count(8, es, 1) == 8);
    }
}

// strong でも path / hub の形を守るか。hub は中心との往復 2(n-1) 本が入る本数のときだけ見る
static void test_digraph_strong_shapes() {
    Gen g(20);
    for (auto [sh, name] : {std::pair{DigraphShape::path, "path"}, std::pair{DigraphShape::hub, "hub"}})
        for (int k = 0; k < 200; ++k) {
            const bool big = k % 2;
            ll n = big ? g.i(40, 60) : g.i(2, 12);
            bool simple = g.coin(0.8);
            ll lo = sh == DigraphShape::hub && big ? 2 * (n - 1) : n;
            ll mx = n * (n - 1) + (simple ? 0 : 5);
            if (big) mx = std::min(mx, lo + n / 2);
            ll m = g.i(lo, mx);
            auto es = g.digraph(n, m, {.shape = sh, .reach = Reach::strong, .simple = simple});
            CHECK_OR(check_edges(n, m, es, true, simple, true) >= 0, continue);
            for (ll v = 1; v <= n; ++v) CHECK(reach_count(n, es, v) == n);
            CHECK(graph_has_shape(n, m, es, name, true, simple, true, true));
        }
}

static void test_digraph_mixed() {
    std::set<std::string> seen;
    const Reach reaches[] = {Reach::none, Reach::weak, Reach::root, Reach::strong};
    for (std::uint64_t seed = 0; seed < 800; ++seed) {
        Gen t(seed);
        const bool big = seed % 2;
        ll n = big ? t.i(40, 60) : t.i(2, 12);
        Reach r = reaches[t.uniform(0, 3)];
        bool simple = t.coin(0.8);
        ll lo = r == Reach::none ? 0 : r == Reach::strong ? n : n - 1;
        ll mx = r == Reach::strong ? n * (n - 1) : n * (n - 1) / 2;  // strong 以外は dag も候補に入る本数まで
        if (big) mx = lo + n / 2;
        if (big && r == Reach::strong && t.coin()) lo = 2 * (n - 1), mx = lo + n / 2;  // hub の判定が効く本数
        ll m = t.i(lo, mx);
        auto es = t.digraph(n, m, {.reach = r, .simple = simple});
        auto s = shape_tag(t, "digraph");
        CHECK_OR(!s.empty(), continue);
        CHECK_OR(check_edges(n, m, es, true, simple, r != Reach::none) >= 0, continue);
        if (r == Reach::root) CHECK(reach_count(n, es, 1) == n);
        if (r == Reach::strong)
            for (ll v = 1; v <= n; ++v) CHECK(reach_count(n, es, v) == n);
        CHECK(graph_has_shape(n, m, es, s, true, simple, r != Reach::none, r == Reach::strong));
        seen.insert(s);
    }
    CHECK(seen.size() == 5);
}

// 大きくても密でも終わる
static void test_graph_big() {
    Gen big(7);
    CHECK((ll)big.graph(200000, 400000, {.shape = GraphShape::path}).size() == 400000);
    CHECK((ll)big.graph(200000, 400000, {.shape = GraphShape::pieces}).size() == 400000);
    CHECK((ll)big.graph(200000, 199999, {.shape = GraphShape::hub, .connected = true}).size() == 199999);
    CHECK((ll)big.graph(300, 300 * 299 / 2, {.shape = GraphShape::path}).size() == 300 * 299 / 2);
    CHECK((ll)big.graph(300, 299 * 298 / 2, {.shape = GraphShape::pieces}).size() == 299 * 298 / 2);
    CHECK((ll)big.digraph(200000, 400000, {.reach = Reach::strong}).size() == 400000);
    CHECK((ll)big.digraph(200000, 400000, {.shape = DigraphShape::path, .reach = Reach::root}).size() == 400000);
    CHECK((ll)big.digraph(300, 300 * 299, {.reach = Reach::strong}).size() == 300 * 299);
}

static void test_dag() {
    Gen g(12);
    for (int k = 0; k < 300; ++k) {
        ll n = g.i(1, 12);
        bool connected = g.coin(), root = connected && g.coin(), increasing = g.coin(), simple = g.coin(0.8);
        ll mx = n * (n - 1) / 2;
        ll lo = connected ? n - 1 : 0;
        ll m = simple ? g.i(lo, mx) : (n == 1 ? 0 : g.i(lo, mx + 5));
        auto es = g.dag(n, m, {.reach = root ? Reach::root : connected ? Reach::weak : Reach::none,
                               .simple = simple,
                               .increasing = increasing});
        CHECK_OR(check_edges(n, m, es, true, simple, connected) >= 0, continue);
        for (auto& e : es) CHECK(e.u != e.v);
        if (increasing)
            for (auto& e : es) CHECK(e.u < e.v);
        if (root) CHECK(reach_count(n, es, 1) == n);
        CHECK(acyclic(n, es));
    }
}

// 二部グラフ（左右別番号 / 通し番号 / 総頂点数だけ / 右をずらした番号）
static void test_bipartite() {
    Gen g(13);
    for (int k = 0; k < 800; ++k) {
        int mode = k % 4;
        ll n1 = g.i(0, 7), n2 = g.i(0, 7);
        if (mode == 2) n1 = g.i(1, 12), n2 = 0;
        if (n1 + n2 == 0) continue;
        ll n = n1 + n2;
        bool connected = g.coin(), simple = g.coin(0.8);
        ll mx = mode == 2 ? (n / 2) * (n - n / 2) : n1 * n2;
        if (connected && n >= 2 && mode != 2 && (n1 == 0 || n2 == 0)) continue;
        bool ordered = (mode == 1 || mode == 2) && g.coin();
        ll lo = connected ? n - 1 : 0;
        if (lo > mx) continue;
        ll m = simple || mx == 0 ? g.i(lo, mx) : g.i(lo, mx + 5);
        const BipartiteLabel label = mode == 0   ? BipartiteLabel::separate
                                     : mode == 3 ? BipartiteLabel::offset
                                                 : BipartiteLabel::merged;
        Edges es = mode == 2 ? g.bipartite(n, m, {.connected = connected, .simple = simple, .ordered = ordered})
                             : g.bipartite(n1, n2, m,
                                           {.connected = connected, .simple = simple, .label = label, .ordered = ordered});
        CHECK((ll)es.size() == m);
        std::set<std::pair<ll, ll>> seen;
        std::vector<ll> p(n);
        std::iota(p.begin(), p.end(), 0);
        std::function<ll(ll)> f = [&](ll x) { return p[x] == x ? x : p[x] = f(p[x]); };
        for (auto& e : es) {
            if (ordered) CHECK(e.u < e.v);
            if (mode == 3) {  // 右は n1+1..n
                CHECK_OR(1 <= e.u && e.u <= n1 && n1 < e.v && e.v <= n, break);
                if (simple) CHECK(seen.insert({e.u, e.v}).second);
                p[f(e.u - 1)] = f(e.v - 1);
            } else if (mode == 0) {
                CHECK_OR(1 <= e.u && e.u <= n1 && 1 <= e.v && e.v <= n2, break);
                if (simple) CHECK(seen.insert({e.u, e.v}).second);
                p[f(e.u - 1)] = f(n1 + e.v - 1);
            } else {
                CHECK_OR(e.u != e.v && 1 <= e.u && e.u <= n && 1 <= e.v && e.v <= n, break);
                if (simple) CHECK(seen.insert({std::min(e.u, e.v), std::max(e.u, e.v)}).second);
                p[f(e.u - 1)] = f(e.v - 1);
            }
        }
        if (mode == 1 || mode == 2) CHECK(two_colorable(neighbors(n, es)));
        if (connected)
            for (ll v = 0; v < n; ++v) CHECK(f(v) == f(0));
    }
}

// 重み（.w）: Range とラムダ、指定しなければ重みなし
static void test_weights() {
    Gen g(14);
    auto in_range = [](const Edges& es, ll lo, ll hi) {
        if (!es.weighted) return false;
        for (auto& e : es)
            if (e.w < lo || hi < e.w) return false;
        return true;
    };
    CHECK(!g.graph(6, 7).weighted);
    CHECK(!g.digraph(6, 7).weighted);
    CHECK(!g.dag(6, 7).weighted);
    CHECK(!g.bipartite(3, 3, 5).weighted);
    CHECK(!g.bipartite(6, 5).weighted);
    for (int k = 0; k < 50; ++k) {
        CHECK(in_range(g.graph(8, 10, {.w = Range{-3, 3}}), -3, 3));
        CHECK(in_range(g.graph(8, 10, {.shape = GraphShape::hub, .connected = true, .w = Range{1, 2}}), 1, 2));
        CHECK(in_range(g.digraph(8, 10, {.w = Range{5, 9}}), 5, 9));
        CHECK(in_range(g.digraph(8, 10, {.reach = Reach::strong, .w = Range{5, 9}}), 5, 9));
        CHECK(in_range(g.dag(8, 10, {.w = Range{0, 0}}), 0, 0));
        for (auto label : {BipartiteLabel::separate, BipartiteLabel::offset, BipartiteLabel::merged})
            CHECK(in_range(g.bipartite(3, 4, 6, {.label = label, .w = Range{1, 1}}), 1, 1));
        CHECK(in_range(g.bipartite(7, 6, {.w = Range{2, 4}}), 2, 4));
        CHECK(in_range(g.graph(8, 10, {.shape = GraphShape::bipartite, .w = Range{2, 4}}), 2, 4));
    }
    // ラムダは辺 1 本につき 1 回呼ばれる
    for (int which = 0; which < 5; ++which) {
        int calls = 0;
        auto w = [&] { return ++calls; };
        Edges es = which == 0   ? g.graph(8, 10, {.w = w})
                   : which == 1 ? g.digraph(8, 10, {.w = w})
                   : which == 2 ? g.dag(8, 10, {.w = w})
                   : which == 3 ? g.bipartite(3, 4, 6, {.w = w})
                                : g.bipartite(7, 6, {.w = w});
        CHECK(es.weighted && calls == (int)es.size());
        std::set<ll> ws;
        for (auto& e : es) ws.insert(e.w);
        CHECK((ll)ws.size() == (ll)es.size());  // 1..m が 1 回ずつ
    }
}

// ---------- 文字列 ----------

static const std::pair<StrShape, const char*> kStrShapes[] = {{StrShape::random, "random"},
                                                              {StrShape::periodic, "periodic"},
                                                              {StrShape::palindrome, "palindrome"},
                                                              {StrShape::fibonacci, "fibonacci"},
                                                              {StrShape::runs, "runs"}};

static void test_str_shapes() {
    Gen g(15);
    for (auto [shape, name] : kStrShapes) {
        for (ll n = 0; n <= 120; ++n) {
            auto s = g.str(n, "abc", {.shape = shape});
            CHECK((ll)s.size() == n);
            for (char c : s) CHECK(c == 'a' || c == 'b' || c == 'c');
            CHECK(str_has_shape(s, name));
        }
    }
    CHECK(g.tags().empty());
    // 判定そのものの確認: ランダムな文字列はほかの形の判定を通らない
    Gen h(16);
    for (auto name : {"periodic", "palindrome", "fibonacci", "runs"})
        CHECK(!str_has_shape(h.str(100, "abc", {.shape = StrShape::random}), name));
    // 1 個だけの回文は回文
    int pal = 0;
    for (int k = 0; k < 200; ++k) pal += is_palindrome(g.str(15, "ab", {.shape = StrShape::palindrome}));
    CHECK(pal > 50);
}

static void test_str_mixed() {
    std::set<std::string> seen;
    for (std::uint64_t seed = 0; seed < 300; ++seed) {
        Gen t(seed);
        auto s = t.str(t.i(1, 120), "abc");
        auto name = shape_tag(t, "str");
        CHECK(t.tags().size() == 1);
        CHECK(str_has_shape(s, name));
        seen.insert(name);
    }
    CHECK(seen.size() == 5);
}

static void test_str_others() {
    Gen g(17);
    auto ss = g.strs(5, 20, "xy");
    ll tot = 0;
    for (auto& s : ss) {
        tot += s.size();
        CHECK(!s.empty());
    }
    CHECK(ss.size() == 5 && tot <= 20);
    auto t = g.mutate("aaaa", 10, "b");
    CHECK(t.size() == 4);
    auto gr = g.grid(3, 4, ".#");
    CHECK(gr.size() == 3 && gr[0].size() == 4);
    CHECK(g.str(5, [&] { return 'z'; }) == "zzzzz");
}

// ---------- 巨大整数 ----------

static void test_big() {
    Gen g(18);
    auto val_less = [](const std::string& x, const std::string& y, std::string_view d) {
        if (x.size() != y.size()) return x.size() < y.size();
        for (size_t k = 0; k < x.size(); ++k)
            if (x[k] != y[k]) return d.find(x[k]) < d.find(y[k]);
        return false;
    };
    for (int rep = 0; rep < 2000; ++rep) {
        const ll len = g.uniform(1, 40);
        auto s = g.big(len);
        CHECK((ll)s.size() == len);
        CHECK(s.size() == 1 || s[0] != '0');
        for (char c : s) CHECK('0' <= c && c <= '9');
        auto h = g.big(5, {.digits = digits::HEX, .lead_zero = true});
        for (char c : h) CHECK(digits::HEX.find(c) != std::string_view::npos);
    }
    bool seen_lead_zero = false, seen_short = false, seen_max = false, seen_small = false;
    const std::string max100 = "1" + std::string(100, '0');
    for (int rep = 0; rep < 5000; ++rep) {
        auto x = g.big_range("1", "1e100");
        CHECK(x[0] != '0' && !val_less(x, "1", digits::dec) && !val_less(max100, x, digits::dec));
        seen_short |= x.size() <= 3;
        seen_max |= x == max100;
        auto y = g.big(3, {.lead_zero = true});
        seen_lead_zero |= y[0] == '0';
        auto z = g.big_range("255", "4096", {.digits = digits::hex});
        CHECK(!val_less(z, "ff", digits::hex) && !val_less("1000", z, digits::hex));
        auto w = g.big_range("0", "1", {.digits = digits::bin});
        CHECK(w == "0" || w == "1");
        seen_small |= w == "0";
        auto v = g.big_range("123456789", "123456790");
        CHECK(v == "123456789" || v == "123456790");
        CHECK(g.big_range("0007", "7") == "7");
    }
    CHECK(seen_lead_zero && seen_short && seen_max && seen_small);
}

static void test_big_conversion() {  // 範囲の書き方と進数の変換
    Gen g(19);
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
            CHECK(g.big_range(vs, vs, {.digits = d}) == conv(v, d));
    }
    CHECK(g.big_range("2^60-1", "2^60-1", {.digits = digits::bin}) == std::string(60, '1'));
    CHECK(g.big_range("2^60", "2^60") == "1152921504606846976");
    CHECK(g.big_range("1e18", "1e18", {.digits = digits::hex}) == "de0b6b3a7640000");
    CHECK(g.big_range("1e18-1", "1e18-1") == std::string(18, '9'));
    CHECK(g.big_range("3^40", "3^40", {.digits = digits::hex}) == conv(12157665459056928801ULL, digits::hex));
    CHECK(g.big_range("1e40", "1e40", {.digits = digits::hex}) == "1d6329f1c35ca4bfabb9f5610000000000");
    CHECK(g.big_range("0e5", "0") == "0");
    for (int rep = 0; rep < 200; ++rep) {
        auto x = g.big_range("0", "16^200000-1", {.digits = digits::hex});
        CHECK(x.size() <= 200000 && (x.size() == 1 || x[0] != '0'));
    }
}

// ---------- 出力とタグ ----------

static void test_output() {
    Gen o(0);
    o.out(1, 2LL, "x", 'c', std::vector<ll>{3, 4}, std::string("yz"));
    o.out(std::vector<int>{});
    Edges es;
    es.push_back({1, 2, 9});
    o.out(es);
    es.weighted = true;
    o.out(es);
    o.out_lines(std::vector<std::string>{"ab", "cd"});
    CHECK(o.buffer() == "1 2 x c 3 4 yz\n\n1 2\n1 2 9\nab\ncd\n");
}

static void test_tags() {  // mixed で選ばれた形と、g.tag で付けたもの
    Gen t(3);
    t.str(10, "ab");
    t.str(10, "ab", {.shape = StrShape::runs});  // 形を指定したらタグは付かない
    t.tag("heavy");
    t.tag("heavy");
    auto& tags = t.tags();
    CHECK_OR(tags.size() == 2, return);
    CHECK(tags[0].first.rfind("str=", 0) == 0 && tags[0].second == 1);
    CHECK(tags[1].first == "heavy" && tags[1].second == 2);
    Gen u(4);
    u.graph(10, 12);
    CHECK(!shape_tag(u, "graph").empty());
    Gen w(5);
    w.graph(10, 12, {.shape = GraphShape::random, .connected = true});
    CHECK(shape_tag(w, "graph").empty() && !shape_tag(w, "graph.base").empty());  // 土台の木の形
}

// ---------- 矛盾した指定は gen error（終了コード 3） ----------

static void test_prime() {
    // is_prime を 10^6 までのふるいと比べる
    const ll N = 1000000;
    std::vector<char> sieve(N + 1, 1);
    sieve[0] = sieve[1] = 0;
    for (ll i = 2; i * i <= N; ++i)
        if (sieve[i])
            for (ll j = i * i; j <= N; j += i) sieve[j] = 0;
    ll bad = 0;
    for (ll x = -5; x <= N; ++x) bad += detail::is_prime(x) != (x >= 0 && sieve[x]);
    CHECK(bad == 0);
    // 大きい素数と、小さい底をすり抜ける合成数
    for (ll p : {998244353LL, 1000000007LL, 1000000000000000003LL, (1LL << 61) - 1, LLONG_MAX - 24})
        CHECK(detail::is_prime(p));
    for (ll c : {3215031751LL, 3825123056546413051LL, 1000000007LL * 998244353LL, LLONG_MAX, (1LL << 62)})
        CHECK(!detail::is_prime(c));

    // 範囲内の素数で、1/8 の端（最小と最大）も含めて全部出る
    std::map<ll, ll> cnt;
    for (std::uint64_t seed = 1; seed <= 3000; ++seed) {
        Gen g(seed);
        ll p = g.prime(100, 200);
        CHECK(100 <= p && p <= 200 && sieve[p]);
        ++cnt[p];
    }
    ll primes_in = 0;
    for (ll x = 100; x <= 200; ++x) primes_in += sieve[x];
    CHECK((ll)cnt.size() == primes_in);
    CHECK(cnt.count(101) && cnt.count(199));

    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        Gen g(seed);
        CHECK(g.prime(2, 2) == 2);
        CHECK(g.prime(-10, 2) == 2);
        CHECK(g.prime(24, 29) == 29);  // x が 24..28 なら上へ、29 ならそのまま
        CHECK(g.prime(23, 28) == 23);  // 上に無いときは下へ
        ll big = g.prime(1000000000000000000LL, LLONG_MAX);
        CHECK(big >= 1000000000000000000LL && detail::is_prime(big));
        CHECK(g.prime(LLONG_MAX - 100, LLONG_MAX) == LLONG_MAX - 24);  // 2^63 - 25 が long long で最大の素数
    }
}

static void test_highly_composite() {
    // 10^6 までは約数の個数を全部数えて、記録を更新する数と比べる
    const ll N = 1000000;
    std::vector<ll> d(N + 1, 0);
    for (ll i = 1; i <= N; ++i)
        for (ll j = i; j <= N; j += i) ++d[j];
    std::vector<ll> want;
    ll best = 0;
    for (ll x = 1; x <= N; ++x)
        if (d[x] > best) best = d[x], want.push_back(x);
    Gen g(1);
    CHECK(g.highly_composite(N) == want);
    CHECK(g.highly_composite(12) == (std::vector<ll>{1, 2, 4, 6, 12}));
    CHECK(g.highly_composite(1) == (std::vector<ll>{1}));
    CHECK(g.highly_composite(13, 60) == (std::vector<ll>{24, 36, 48, 60}));
    CHECK(g.highly_composite(24, 24) == (std::vector<ll>{24}));

    // 10^18 以下は 156 個で、最大は 897612484786617600（約数 103680 個）
    auto v = g.highly_composite(1000000000000000000LL);
    CHECK(v.size() == 156);
    CHECK(v.back() == 897612484786617600LL);
    // 約数の個数（素因数は 53 以下だけ）が狭義単調増加
    auto divs = [](ll x) {
        ll r = 1;
        for (ll p : {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53}) {
            ll e = 0;
            while (x % p == 0) x /= p, ++e;
            r *= e + 1;
        }
        return x == 1 ? r : -1;
    };
    CHECK(divs(v.back()) == 103680);
    auto all = g.highly_composite(LLONG_MAX);
    CHECK(std::is_sorted(all.begin(), all.end()));
    for (std::size_t k = 1; k < all.size(); ++k) CHECK_OR(divs(all[k - 1]) < divs(all[k]) && divs(all[k - 1]) > 0, break);
}

static void test_gen_errors() {
    auto dies = [](auto f) { return dies_with_gen_error([f] { Gen g(1); f(g); }); };
    // 判定そのものの確認: 正しい指定では止まらない
    CHECK(!dies([](Gen& g) { g.graph(5, 4, {.connected = true}); }));
    // 範囲
    CHECK(dies([](Gen& g) { g.uniform(3, 2); }));
    CHECK(dies([](Gen& g) { g.i(3, 2); }));
    CHECK(dies([](Gen& g) { g.seq(3, 5, 4); }));
    CHECK(dies([](Gen& g) { g.distinct(5, 1, 4); }));
    CHECK(dies([](Gen& g) { g.sorted(5, 1, 4, {.strict = true}); }));
    CHECK(dies([](Gen& g) { g.partition(3, 0); }));
    CHECK(dies([](Gen& g) { g.partition(3, 2, 2); }));
    CHECK(dies([](Gen& g) { g.decimal(0, 1, 19); }));
    CHECK(dies([](Gen& g) { g.decimal(0.11, 0.19, 1); }));
    CHECK(dies([](Gen& g) { g.choice(std::vector<ll>{}); }));
    // 木
    CHECK(dies([](Gen& g) { g.tree(0); }));
    CHECK(dies([](Gen& g) { g.parents(0); }));
    // 無向グラフ
    CHECK(dies([](Gen& g) { g.graph(0, 0); }));
    CHECK(dies([](Gen& g) { g.graph(4, 7); }));                       // 単純グラフの最大 6 本を超える
    CHECK(dies([](Gen& g) { g.graph(5, 3, {.connected = true}); }));  // 連結には n-1 本必要
    CHECK(dies([](Gen& g) { g.graph(5, 4, {.shape = GraphShape::pieces, .connected = true}); }));
    CHECK(dies([](Gen& g) { g.graph(1, 0, {.shape = GraphShape::pieces}); }));
    CHECK(dies([](Gen& g) { g.graph(5, 7, {.shape = GraphShape::pieces}); }));     // 4+1 に分けても 6 本まで
    CHECK(dies([](Gen& g) { g.graph(5, 7, {.shape = GraphShape::bipartite}); }));  // 2*3 = 6 本まで
    // 有向グラフ
    CHECK(dies([](Gen& g) { g.digraph(3, 7); }));
    CHECK(dies([](Gen& g) { g.digraph(5, 4, {.reach = Reach::strong}); }));  // 強連結には m >= n
    CHECK(dies([](Gen& g) { g.digraph(5, 3, {.reach = Reach::root}); }));
    CHECK(dies([](Gen& g) { g.digraph(5, 6, {.shape = DigraphShape::pieces, .reach = Reach::weak}); }));
    CHECK(dies([](Gen& g) { g.digraph(5, 6, {.shape = DigraphShape::dag, .reach = Reach::strong}); }));
    // DAG
    CHECK(dies([](Gen& g) { g.dag(4, 7); }));
    CHECK(dies([](Gen& g) { g.dag(1, 1, {.simple = false}); }));
    CHECK(dies([](Gen& g) { g.dag(5, 6, {.reach = Reach::strong}); }));
    CHECK(dies([](Gen& g) { g.dag(5, 3, {.reach = Reach::weak}); }));
    // 二部グラフ
    CHECK(dies([](Gen& g) { g.bipartite(0, 0, 0); }));
    CHECK(dies([](Gen& g) { g.bipartite(2, 3, 7); }));
    CHECK(dies([](Gen& g) { g.bipartite(2, 0, 1, {.simple = false}); }));  // 片側が空なら辺はない
    CHECK(dies([](Gen& g) { g.bipartite(2, 3, 3, {.connected = true}); }));
    CHECK(dies([](Gen& g) { g.bipartite(2, 0, 1, {.connected = true}); }));
    CHECK(dies([](Gen& g) { g.bipartite(2, 3, 4, {.ordered = true}); }));  // separate では使えない
    CHECK(dies([](Gen& g) { g.bipartite(2, 3, 4, {.label = BipartiteLabel::offset, .ordered = true}); }));
    CHECK(!dies([](Gen& g) { g.bipartite(2, 3, 4, {.label = BipartiteLabel::merged, .ordered = true}); }));
    CHECK(dies([](Gen& g) { g.bipartite(5, 7); }));
    // 文字列と巨大整数
    CHECK(dies([](Gen& g) { g.str(3, ""); }));
    CHECK(dies([](Gen& g) { g.strs(5, 4, "ab"); }));
    CHECK(dies([](Gen& g) { g.big(0); }));
    CHECK(dies([](Gen& g) { g.big(3, {.digits = "0"}); }));
    CHECK(dies([](Gen& g) { g.big_range("10", "9"); }));
    CHECK(dies([](Gen& g) { g.big_range("1+1", "9"); }));
    CHECK(dies([](Gen& g) { g.prime(3, 2); }));
    CHECK(dies([](Gen& g) { g.prime(1, 1); }));
    CHECK(dies([](Gen& g) { g.prime(24, 28); }));
    CHECK(dies([](Gen& g) { g.prime(LLONG_MAX - 10, LLONG_MAX); }));
    CHECK(dies([](Gen& g) { g.highly_composite(0); }));
    CHECK(dies([](Gen& g) { g.highly_composite(5, 3); }));
    CHECK(dies([](Gen& g) { g.highly_composite(13, 23); }));
}

int main(int argc, char** argv) {
    return testing::run_tests(argc, argv,
                              {
                                  {"seed", test_seed},
                                  {"range", test_range},
                                  {"size_cap", test_size_cap},
                                  {"decimal", test_decimal},
                                  {"distinct_perm_partition", test_distinct_perm_partition},
                                  {"sorted", test_sorted},
                                  {"seq_shapes", test_seq_shapes},
                                  {"seq_mixed", test_seq_mixed},
                                  {"tree_shapes", test_tree_shapes},
                                  {"tree_mixed", test_tree_mixed},
                                  {"tree_options", test_tree_options},
                                  {"parents", test_parents},
                                  {"parents_mixed", test_parents_mixed},
                                  {"one_indexed", test_one_indexed},
                                  {"graph_basic", test_graph_basic},
                                  {"graph_shapes", test_graph_shapes},
                                  {"graph_mixed", test_graph_mixed},
                                  {"digraph_shapes", test_digraph_shapes},
                                  {"digraph_strong_shapes", test_digraph_strong_shapes},
                                  {"digraph_mixed", test_digraph_mixed},
                                  {"graph_big", test_graph_big},
                                  {"dag", test_dag},
                                  {"bipartite", test_bipartite},
                                  {"weights", test_weights},
                                  {"str_shapes", test_str_shapes},
                                  {"str_mixed", test_str_mixed},
                                  {"str_others", test_str_others},
                                  {"big", test_big},
                                  {"big_conversion", test_big_conversion},
                                  {"prime", test_prime},
                                  {"highly_composite", test_highly_composite},
                                  {"output", test_output},
                                  {"tags", test_tags},
                                  {"gen_errors", test_gen_errors},
                              });
}
