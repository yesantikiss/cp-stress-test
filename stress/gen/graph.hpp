#pragma once

// 木とグラフ: tree / parents / graph / digraph / dag / bipartite

#include "core.hpp"

namespace stress {

enum class TreeShape { mixed, random, path, star, caterpillar, binary, deep };
enum class GraphShape { mixed, random, bipartite, pieces, hub, path };
enum class DigraphShape { mixed, random, dag, pieces, hub, path };
// 有向グラフの到達性。none: 指定なし、weak: 弱連結、root: 頂点 1 から全頂点に到達できる、strong: 強連結。
enum class Reach { none, weak, root, strong };
// 二部グラフの頂点番号。separate: 左 1..n1 と右 1..n2 の別番号、offset: 左 1..n1 と右 n1+1..n1+n2、
// merged: 1..n1+n2 をシャッフルした通し番号。
enum class BipartiteLabel { separate, offset, merged };

struct Edge {
    ll u, v, w;
};

struct Edges : std::vector<Edge> {
    bool weighted = false;
};

struct TreeOpt {
    TreeShape shape = TreeShape::mixed;
    bool ordered = false;  // true なら各辺 "u v" を常に u < v で出す
    Value w{};
};

struct ParentOpt {
    TreeShape shape = TreeShape::mixed;
    bool ordered = true;  // true なら P_i < i で、根を除いた n-1 個を返す
};

struct GraphOpt {
    GraphShape shape = GraphShape::mixed;
    bool connected = false;
    bool simple = true;    // 自己ループと多重辺なし
    bool ordered = false;  // true なら各辺 "u v" を常に u < v（自己ループは u = v）で出す
    Value w{};
};

struct DigraphOpt {
    DigraphShape shape = DigraphShape::mixed;
    Reach reach = Reach::none;
    bool simple = true;  // 自己ループと多重辺なし
    Value w{};
};

struct DagOpt {
    Reach reach = Reach::none;  // weak / root まで（strong はエラー）
    bool simple = true;         // 多重辺なし（自己ループはどちらでも出ない）
    bool increasing = false;    // true なら常に u < v（頂点番号がそのままトポ順）
    Value w{};
};

struct BipartiteOpt {
    bool connected = false;
    bool simple = true;  // 多重辺なし
    BipartiteLabel label = BipartiteLabel::separate;
    bool ordered = false;  // true なら各辺 "u v" を常に u < v で出す（label が merged のときだけ）
    Value w{};
};

template <class G>
class GraphOps {
    G& self() { return static_cast<G&>(*this); }

  public:

    Edges tree(ll n, TreeOpt opt = {}) {
        if (n <= 0) gen_error("tree: n must be positive");
        auto raw = raw_tree(n, opt.shape, "tree");
        return finish_edges(n, raw, opt.w, /*shuffle_labels=*/true, /*directed=*/false, -1, opt.ordered);
    }

    // 根付き木の親の列。
    //   ordered = true : 根が 1 で P_i < i。P_2 .. P_n の n-1 個を返す。
    //   ordered = false: 番号はシャッフル、根もランダム。n 個を返し、根の親は base() - 1（0 か -1）。
    std::vector<ll> parents(ll n, ParentOpt opt = {}) {
        if (n <= 0) gen_error("parents: n must be positive");
        auto raw = raw_tree(n, opt.shape, "parents");
        std::vector<std::vector<ll>> adj(n);
        for (auto [u, v] : raw) adj[u].push_back(v), adj[v].push_back(u);
        // path や deep は頂点 0 を根にすると深くなるので、半々で 0 を根にする。
        const ll root = self().coin() ? 0 : self().uniform(0, n - 1);
        std::vector<ll> par(n, -1), order;  // order: 親が子より先に来る順
        order.reserve(n);
        if (opt.ordered) {
            // 根から、まだ番号のない子をランダムに選びながら番号を振る（ランダムなトポ順）。
            std::vector<ll> frontier{root};
            std::vector<char> seen(n, 0);
            seen[root] = 1;
            while (!frontier.empty()) {
                const ll k = self().uniform(0, static_cast<ll>(frontier.size()) - 1);
                std::swap(frontier[k], frontier.back());
                const ll v = frontier.back();
                frontier.pop_back();
                order.push_back(v);
                for (ll c : adj[v])
                    if (!seen[c]) seen[c] = 1, par[c] = v, frontier.push_back(c);
            }
            std::vector<ll> label(n);
            for (ll k = 0; k < n; ++k) label[order[k]] = k;
            std::vector<ll> p(n - 1);
            for (ll k = 1; k < n; ++k) p[k - 1] = label[par[order[k]]] + self().base();
            return p;
        }
        std::vector<ll> stack{root};
        std::vector<char> seen(n, 0);
        seen[root] = 1;
        while (!stack.empty()) {
            const ll v = stack.back();
            stack.pop_back();
            for (ll c : adj[v])
                if (!seen[c]) seen[c] = 1, par[c] = v, stack.push_back(c);
        }
        std::vector<ll> label(n);
        std::iota(label.begin(), label.end(), 0);
        self().shuffle(label);
        std::vector<ll> p(n);
        for (ll v = 0; v < n; ++v) p[label[v]] = par[v] < 0 ? self().base() - 1 : label[par[v]] + self().base();
        return p;
    }

    // 無向グラフ。
    Edges graph(ll n, ll m, GraphOpt opt = {}) {
        if (n <= 0) gen_error("graph: n must be positive");
        const Spec sp{.name = "graph",
                      .directed = false,
                      .connected = opt.connected,
                      .simple = opt.simple,
                      .ordered = opt.ordered,
                      .w = opt.w};
        check_graph(n, m, sp);
        GraphShape shape = opt.shape;
        if (shape == GraphShape::mixed) {
            // random を 2 つ入れて、素のランダムが少し多めに出るようにする
            std::vector<GraphShape> cand{GraphShape::random, GraphShape::random, GraphShape::hub, GraphShape::path};
            if (bipartite_fits(n, m, sp.simple)) cand.push_back(GraphShape::bipartite);
            if (pieces_fits(n, m, sp)) cand.push_back(GraphShape::pieces);
            shape = self().choice(cand);
            self().tag(std::string("graph=") + shape_name(shape));
        }
        switch (shape) {
            case GraphShape::bipartite:
                return bipartite(n, m,
                                 {.connected = opt.connected, .simple = opt.simple, .ordered = opt.ordered, .w = opt.w});
            case GraphShape::pieces: return pieces_graph(n, m, sp);
            case GraphShape::hub: return hub_graph(n, m, sp);
            case GraphShape::path: return path_graph(n, m, sp);
            default: return random_graph(n, m, sp);
        }
    }

    // 有向グラフ。
    Edges digraph(ll n, ll m, DigraphOpt opt = {}) {
        if (n <= 0) gen_error("digraph: n must be positive");
        const Spec sp{.name = "digraph",
                      .directed = true,
                      .connected = opt.reach != Reach::none,
                      .root = opt.reach == Reach::root,
                      .strong = opt.reach == Reach::strong,
                      .simple = opt.simple,
                      .w = opt.w};
        check_graph(n, m, sp);
        if (sp.strong && n >= 2 && m < n) gen_error("digraph: strongly connected graph needs m >= n (n >= 2)");
        DigraphShape shape = opt.shape;
        if (shape == DigraphShape::mixed) {
            std::vector<DigraphShape> cand{DigraphShape::random, DigraphShape::random, DigraphShape::hub,
                                           DigraphShape::path};
            if (!sp.strong && dag_fits(n, m, sp.simple)) cand.push_back(DigraphShape::dag);
            if (pieces_fits(n, m, sp)) cand.push_back(DigraphShape::pieces);
            shape = self().choice(cand);
            self().tag(std::string("digraph=") + shape_name(shape));
        }
        switch (shape) {
            case DigraphShape::dag:
                if (sp.strong) gen_error("digraph: shape dag can't be strongly connected");
                return dag(n, m, {.reach = opt.reach, .simple = opt.simple, .w = opt.w});
            case DigraphShape::pieces: return pieces_graph(n, m, sp);
            case DigraphShape::hub: return hub_graph(n, m, sp);
            case DigraphShape::path: return path_graph(n, m, sp);
            default: return random_graph(n, m, sp);
        }
    }

    // ランダムなトポ順の上で、前から後ろへの辺だけを張る。
    Edges dag(ll n, ll m, DagOpt opt = {}) {
        if (n <= 0) gen_error("dag: n must be positive");
        const ll max_edges = n * (n - 1) / 2;
        if (m > max_edges && (opt.simple || n == 1))
            gen_error("dag: m=" + std::to_string(m) + " exceeds the maximum " + std::to_string(max_edges) +
                      (opt.simple ? " for a simple DAG" : " (n=1 has no edges)"));
        if (opt.reach == Reach::strong) gen_error("dag: a DAG can't be strongly connected");
        const bool root = opt.reach == Reach::root, connected = opt.reach != Reach::none;
        if (connected && m < n - 1) gen_error("dag: connected DAG needs m >= n-1");
        // 頂点 i はトポ順の i 番目。辺は常に (小, 大)。
        std::vector<std::pair<ll, ll>> raw;
        if (root) {
            // 頂点 0（トポ順の先頭）を根にして、各頂点に前の頂点から 1 本ずつ入れる
            for (ll v = 1; v < n; ++v) raw.push_back({self().uniform(0, v - 1), v});
        } else if (connected) {
            raw = spanning_tree(n);
        }
        for (auto& [u, v] : raw)
            if (u > v) std::swap(u, v);
        fill_edges(raw, m, max_edges, opt.simple, /*ordered=*/false,
                   [&] {
                       ll u = self().uniform(0, n - 1), v = self().uniform(0, n - 2);
                       if (v >= u) ++v;
                       return std::pair{std::min(u, v), std::max(u, v)};
                   },
                   [&](auto&& emit) {
                       for (ll u = 0; u < n; ++u)
                           for (ll v = u + 1; v < n; ++v) emit(u, v);
                   });
        return finish_edges(n, raw, opt.w, /*shuffle_labels=*/!opt.increasing, /*directed=*/true, root ? 0 : -1);
    }

    // 左 n1 頂点、右 n2 頂点の二部グラフ。既定では各辺 "u v" の u が左 (1..n1)、v が右 (1..n2)。
    // offset なら右は n1+1..n1+n2。
    Edges bipartite(ll n1, ll n2, ll m, BipartiteOpt opt = {}) {
        if (n1 < 0 || n2 < 0 || n1 + n2 <= 0) gen_error("bipartite: n1, n2 must be >= 0 and n1+n2 > 0");
        if (opt.ordered && opt.label != BipartiteLabel::merged)
            gen_error("bipartite: .ordered needs .label = BipartiteLabel::merged "
                      "(separate keeps u on the left, offset is always u < v)");
        const ll max_edges = n1 * n2;
        if (m > max_edges && (opt.simple || max_edges == 0))
            gen_error("bipartite: m=" + std::to_string(m) + " exceeds the maximum " + std::to_string(max_edges) +
                      (opt.simple ? " for a simple bipartite graph" : " (one side is empty)"));
        if (opt.connected && m < n1 + n2 - 1) gen_error("bipartite: connected graph needs m >= n1+n2-1");
        if (opt.connected && n1 + n2 >= 2 && (n1 == 0 || n2 == 0))
            gen_error("bipartite: connected graph needs both sides non-empty");
        // raw は (左の番号, 右の番号)、どちらも 0 始まり。
        std::vector<std::pair<ll, ll>> raw;
        if (opt.connected && n1 > 0 && n2 > 0) {
            std::vector<ll> placed_l{self().uniform(0, n1 - 1)}, placed_r{self().uniform(0, n2 - 1)};
            raw.push_back({placed_l[0], placed_r[0]});
            std::vector<ll> rest;  // 左は a、右は n1 + b
            for (ll a = 0; a < n1; ++a)
                if (a != placed_l[0]) rest.push_back(a);
            for (ll b = 0; b < n2; ++b)
                if (b != placed_r[0]) rest.push_back(n1 + b);
            self().shuffle(rest);
            for (ll x : rest) {
                if (x < n1) {
                    raw.push_back({x, self().choice(placed_r)});
                    placed_l.push_back(x);
                } else {
                    raw.push_back({self().choice(placed_l), x - n1});
                    placed_r.push_back(x - n1);
                }
            }
        }
        fill_edges(raw, m, max_edges, opt.simple, /*ordered=*/true,
                   [&] { return std::pair{self().uniform(0, n1 - 1), self().uniform(0, n2 - 1)}; },
                   [&](auto&& emit) {
                       for (ll a = 0; a < n1; ++a)
                           for (ll b = 0; b < n2; ++b) emit(a, b);
                   });
        if (opt.label == BipartiteLabel::merged) {
            for (auto& e : raw) e.second += n1;
            return finish_edges(n1 + n2, raw, opt.w, /*shuffle_labels=*/true, /*directed=*/false, -1, opt.ordered);
        }
        const ll right_base = opt.label == BipartiteLabel::offset ? n1 : 0;
        std::vector<ll> label_l(n1), label_r(n2);
        std::iota(label_l.begin(), label_l.end(), 0);
        std::iota(label_r.begin(), label_r.end(), 0);
        self().shuffle(label_l);
        self().shuffle(label_r);
        self().shuffle(raw);
        Edges es;
        es.weighted = !std::holds_alternative<std::monostate>(opt.w);
        es.reserve(raw.size());
        for (auto [a, b] : raw) {
            Edge e{label_l[a] + self().base(), right_base + label_r[b] + self().base(), 0};
            if (es.weighted) e.w = self().draw(opt.w);
            es.push_back(e);
        }
        return es;
    }

    // 総頂点数 n だけ指定する二部グラフ。左右の分け方はランダムで、頂点番号は通し (1..n) になる。
    Edges bipartite(ll n, ll m, BipartiteOpt opt = {}) {
        if (n <= 0) gen_error("bipartite: n must be positive");
        if (n == 1) {
            opt.label = BipartiteLabel::merged;
            return bipartite(1, 0, m, opt);
        }
        std::vector<ll> ok;  // m 本入る左の大きさ
        for (ll n1 = 1; n1 < n; ++n1)
            if (n1 * (n - n1) >= m || !opt.simple) ok.push_back(n1);
        if (ok.empty())
            gen_error("bipartite: m=" + std::to_string(m) + " exceeds the maximum " +
                      std::to_string((n / 2) * (n - n / 2)) + " for a simple bipartite graph with n=" +
                      std::to_string(n));
        const ll n1 = self().choice(ok);
        opt.label = BipartiteLabel::merged;
        return bipartite(n1, n - n1, m, opt);
    }

  private:
    using RawEdges = std::vector<std::pair<ll, ll>>;

    // graph / digraph の形の共通の指定。
    struct Spec {
        const char* name;    // エラーとタグ用（"graph" / "digraph"）
        bool directed;
        bool connected;      // 無向なら連結、有向なら少なくとも弱連結
        bool root = false;   // 有向: 頂点 0 から全頂点に到達できる
        bool strong = false; // 有向: 強連結
        bool simple;
        bool ordered = false;  // 無向: 各辺を u < v で出す
        Value w;
    };

    void check_graph(ll n, ll m, const Spec& sp) {
        const std::string name = sp.name;
        const ll max_edges = sp.directed ? n * (n - 1) : n * (n - 1) / 2;
        if (sp.simple && m > max_edges)
            gen_error(name + ": m=" + std::to_string(m) + " exceeds the maximum " + std::to_string(max_edges) +
                      " for a simple graph");
        if (sp.connected && m < n - 1) gen_error(name + ": connected graph needs m >= n-1");
    }

    static const char* shape_name(TreeShape s) {
        switch (s) {
            case TreeShape::random: return "random";
            case TreeShape::path: return "path";
            case TreeShape::star: return "star";
            case TreeShape::caterpillar: return "caterpillar";
            case TreeShape::binary: return "binary";
            case TreeShape::deep: return "deep";
            default: return "mixed";
        }
    }
    static const char* shape_name(GraphShape s) {
        switch (s) {
            case GraphShape::random: return "random";
            case GraphShape::bipartite: return "bipartite";
            case GraphShape::pieces: return "pieces";
            case GraphShape::hub: return "hub";
            case GraphShape::path: return "path";
            default: return "mixed";
        }
    }

    static const char* shape_name(DigraphShape s) {
        switch (s) {
            case DigraphShape::random: return "random";
            case DigraphShape::dag: return "dag";
            case DigraphShape::pieces: return "pieces";
            case DigraphShape::hub: return "hub";
            case DigraphShape::path: return "path";
            default: return "mixed";
        }
    }

    static bool dag_fits(ll n, ll m, bool simple) {
        if (m == 0) return true;
        return n >= 2 && (!simple || m <= n * (n - 1) / 2);
    }
    static bool bipartite_fits(ll n, ll m, bool simple) {
        if (m == 0) return true;
        return n >= 2 && (!simple || m <= (n / 2) * (n - n / 2));
    }
    // 2 つ以上の組に分けて m 本入るか（一番入るのは 1 頂点と n-1 頂点に分けたとき）。
    static bool pieces_fits(ll n, ll m, const Spec& opt) {
        if (opt.connected || n < 2) return false;
        const ll big = (n - 1) * (n - 2) / 2 * (opt.directed ? 2 : 1);
        return !opt.simple || m <= big;
    }

    // 素のランダム。connected なら形を混ぜた木を土台にする。
    Edges random_graph(ll n, ll m, const Spec& opt) {
        RawEdges raw;
        raw.reserve(m);
        ll root = -1;
        if (opt.connected) raw = base_edges(n, m, opt.strong ? RawEdges{} : raw_tree(n, TreeShape::mixed, (std::string(opt.name) + ".base").c_str()), opt, root);
        grow(raw, m, opt, [&] { return std::pair{self().uniform(0, n - 1), self().uniform(0, n - 1)}; },
             [&](auto&& emit) {
                 for (ll u = 0; u < n; ++u)
                     for (ll v = 0; v < n; ++v) emit(u, v);
             });
        return finish_edges(n, raw, opt.w, /*shuffle_labels=*/true, opt.directed, root, opt.ordered);
    }

    // 頂点を 2 つ以上の組に分け、組の中だけに辺を張る。連結成分が複数になる。
    Edges pieces_graph(ll n, ll m, const Spec& opt) {
        const std::string name = opt.name;
        if (opt.connected)
            gen_error(name + ": shape pieces can't be used with " + (opt.directed ? ".reach" : ".connected = true"));
        if (n < 2) gen_error(name + ": shape pieces needs n >= 2");
        if (!pieces_fits(n, m, opt))
            gen_error(name + ": m=" + std::to_string(m) + " is too many for shape pieces with n=" + std::to_string(n));
        auto capacity = [&](const std::vector<ll>& sizes) {
            ll c = 0;
            for (ll s : sizes) c += s * (s - 1) / 2 * (opt.directed ? 2 : 1);
            return c;
        };
        std::vector<ll> sizes;
        for (int t = 0; t < 20 && (sizes.empty() || (opt.simple && capacity(sizes) < m)); ++t) {
            const ll k = self().coin() ? self().uniform(2, std::min<ll>(n, 4)) : self().uniform(2, n);
            sizes = self().partition(n, k, 1);
        }
        if (opt.simple && capacity(sizes) < m) sizes = {n - 1, 1};
        std::vector<ll> lo(n), hi(n);  // 頂点 v の組は [lo[v], hi[v]]
        for (ll start = 0; ll s : sizes) {
            for (ll v = start; v < start + s; ++v) lo[v] = start, hi[v] = start + s - 1;
            start += s;
        }
        RawEdges raw;
        raw.reserve(m);
        grow(raw, m, opt,
             [&] {
                 const ll u = self().uniform(0, n - 1);
                 return std::pair{u, self().uniform(lo[u], hi[u])};
             },
             [&](auto&& emit) {
                 for (ll u = 0; u < n; ++u)
                     for (ll v = lo[u]; v <= hi[u]; ++v) emit(u, v);
             });
        return finish_edges(n, raw, opt.w, /*shuffle_labels=*/true, opt.directed, -1, opt.ordered);
    }

    // 辺の多くが 1 から 3 個の頂点につながる（次数が偏る）。connected ならスターが土台。
    Edges hub_graph(ll n, ll m, const Spec& opt) {
        const ll h = self().uniform(1, std::min<ll>(n, 3));  // 頂点 0 .. h-1 が中心
        RawEdges raw;
        raw.reserve(m);
        ll root = -1;
        if (opt.connected) raw = base_edges(n, m, opt.strong ? RawEdges{} : raw_tree(n, TreeShape::star, nullptr), opt, root);
        auto orient = [&](std::pair<ll, ll> e) {
            if (opt.directed && self().coin()) std::swap(e.first, e.second);
            return e;
        };
        grow(raw, m, opt,
             [&] {
                 if (self().coin(0.8)) return orient({self().uniform(0, h - 1), self().uniform(0, n - 1)});
                 return std::pair{self().uniform(0, n - 1), self().uniform(0, n - 1)};
             },
             [&](auto&& emit) {
                 for (ll u = 0; u < n; ++u)
                     for (ll v = 0; v < n; ++v) emit(u, v);
             });
        return finish_edges(n, raw, opt.w, /*shuffle_labels=*/true, opt.directed, root, opt.ordered);
    }

    // 頂点を一列に並べ、番号の差が w 以下の頂点どうしだけに辺を張る（直径が長い）。
    // connected ならパスが土台。w は 1 から 3 で、m 本入らないときだけ広げる。
    Edges path_graph(ll n, ll m, const Spec& opt) {
        auto capacity = [&](ll w) { return (w * n - w * (w + 1) / 2) * (opt.directed ? 2 : 1); };
        ll w = std::min<ll>(self().uniform(1, 3), std::max<ll>(n - 1, 1));
        if (opt.simple)
            while (w < n - 1 && capacity(w) < m) ++w;
        RawEdges raw;
        raw.reserve(m);
        ll root = -1;
        if (opt.connected) raw = base_edges(n, m, opt.strong ? RawEdges{} : raw_tree(n, TreeShape::path, nullptr), opt, root);
        auto orient = [&](std::pair<ll, ll> e) {
            if (opt.directed && self().coin()) std::swap(e.first, e.second);
            return e;
        };
        grow(raw, m, opt,
             [&] {
                 if (n == 1) return std::pair<ll, ll>{0, 0};
                 const ll u = self().uniform(0, n - 2);
                 return orient({u, u + self().uniform(1, std::min(w, n - 1 - u))});
             },
             [&](auto&& emit) {
                 for (ll u = 0; u < n; ++u)
                     for (ll v = std::max<ll>(0, u - w); v <= std::min(n - 1, u + w); ++v) emit(u, v);
             });
        return finish_edges(n, raw, opt.w, /*shuffle_labels=*/true, opt.directed, root, opt.ordered);
    }

    // connected のときの土台。tree は無向の全域木（strong のときは空で、使わない）。
    //   無向: tree のまま
    //   有向: strong なら耳分解で作る強連結グラフ、root なら根 root から外向きの木、どちらでもなければ向きはランダム
    // root を付けたときは根の添字を root に入れる（finish_edges で番号 1 にする）。
    RawEdges base_edges(ll n, ll m, RawEdges tree, const Spec& opt, ll& root) {
        if (!opt.directed) return tree;
        if (opt.strong) return ears(n, m);
        if (!opt.root) {
            for (auto& [u, v] : tree)
                if (self().coin()) std::swap(u, v);
            return tree;
        }
        root = self().coin() ? 0 : self().uniform(0, n - 1);  // path なら半々で端が根
        std::vector<std::vector<ll>> adj(n);
        for (auto [u, v] : tree) adj[u].push_back(v), adj[v].push_back(u);
        RawEdges out;
        out.reserve(n - 1);
        std::vector<ll> stack{root};
        std::vector<char> seen(n, 0);
        seen[root] = 1;
        while (!stack.empty()) {
            const ll v = stack.back();
            stack.pop_back();
            for (ll c : adj[v])
                if (!seen[c]) seen[c] = 1, out.push_back({v, c}), stack.push_back(c);
        }
        return out;
    }

    // 耳分解で強連結な有向グラフを作る。頂点 0 から始め、新しい頂点の列 x1..xk を
    // 「今ある頂点 → x1 → ... → xk → 今ある頂点」の形で足していく。辺は (n-1) + 耳の数 本。
    // 自己ループと多重辺は出ない。
    RawEdges ears(ll n, ll m) {
        RawEdges raw;
        if (n == 1) return raw;
        const ll e = self().uniform(1, std::min(n - 1, m - (n - 1)));  // 耳の数
        std::vector<ll> order(n - 1);
        std::iota(order.begin(), order.end(), 1);
        self().shuffle(order);
        auto lens = self().partition(n - 1, e, 1);
        std::vector<ll> placed{0};
        ll k = 0;
        for (ll len : lens) {
            ll prev = self().choice(placed);
            const ll end = self().choice(placed);
            for (ll t = 0; t < len; ++t) {
                const ll x = order[k++];
                raw.push_back({prev, x});
                prev = x;
            }
            raw.push_back({prev, end});
            for (ll t = k - len; t < k; ++t) placed.push_back(order[t]);
        }
        return raw;
    }

    // graph 用: raw に draw() の辺を足して m 本にする。simple なら自己ループと多重辺を除く。
    // 続けて 64 回外れたら密とみなし、each(emit) で列挙した候補の残りから選ぶ。
    template <class Draw, class Each>
    void grow(RawEdges& raw, ll m, const Spec& opt, Draw draw, Each each) {
        if (!opt.simple) {
            while (static_cast<ll>(raw.size()) < m) raw.push_back(draw());
            return;
        }
        std::set<std::pair<ll, ll>> used;
        auto key = [&](ll u, ll v) {
            return opt.directed ? std::pair{u, v} : std::pair{std::min(u, v), std::max(u, v)};
        };
        for (auto [u, v] : raw) used.insert(key(u, v));
        int miss = 0;
        while (static_cast<ll>(raw.size()) < m) {
            auto [u, v] = draw();
            if (u != v && used.insert(key(u, v)).second) {
                raw.push_back({u, v});
                miss = 0;
                continue;
            }
            if (++miss < 64) continue;
            RawEdges cand;
            each([&](ll a, ll b) {
                if (a != b && used.insert(key(a, b)).second) cand.push_back({a, b});
            });
            const ll need = m - static_cast<ll>(raw.size());
            if (static_cast<ll>(cand.size()) < need) gen_error("graph: not enough candidate edges");
            self().shuffle(cand);
            cand.resize(need);
            raw.insert(raw.end(), cand.begin(), cand.end());
            return;
        }
    }

    // 形 shape_in の木の辺（0 始まり、作った順）。mixed で選んだ形は "tag_name=形" のタグにする。
    RawEdges raw_tree(ll n, TreeShape shape_in, const char* tag_name) {
        TreeShape shape = shape_in;
        if (shape == TreeShape::mixed) {
            static constexpr TreeShape all[] = {TreeShape::random, TreeShape::random, TreeShape::path,
                                                TreeShape::star, TreeShape::caterpillar, TreeShape::binary,
                                                TreeShape::deep};
            shape = all[self().uniform(0, std::size(all) - 1)];
            if (tag_name) self().tag(std::string(tag_name) + "=" + shape_name(shape));
        }
        std::vector<std::pair<ll, ll>> raw;  // 0-indexed, 作った順
        raw.reserve(n > 0 ? n - 1 : 0);
        switch (shape) {
            case TreeShape::random: raw = prufer_tree(n); break;
            case TreeShape::path:
                for (ll v = 1; v < n; ++v) raw.push_back({v - 1, v});
                break;
            case TreeShape::star:
                for (ll v = 1; v < n; ++v) raw.push_back({0, v});
                break;
            case TreeShape::caterpillar: {
                const ll spine = std::max<ll>(1, self().uniform(1, std::max<ll>(1, n / 2)));
                for (ll v = 1; v < spine; ++v) raw.push_back({v - 1, v});
                for (ll v = spine; v < n; ++v) raw.push_back({self().uniform(0, spine - 1), v});
                break;
            }
            case TreeShape::binary:
                for (ll v = 1; v < n; ++v) raw.push_back({(v - 1) / 2, v});
                break;
            case TreeShape::deep: {
                const ll width = self().uniform(1, 3);
                for (ll v = 1; v < n; ++v) raw.push_back({self().uniform(std::max<ll>(0, v - width), v - 1), v});
                break;
            }
            case TreeShape::mixed: break;
        }
        return raw;
    }

    // ランダムな全域木の辺（0 始まり、頂点番号はそのまま）。
    std::vector<std::pair<ll, ll>> spanning_tree(ll n) {
        std::vector<ll> label(n);
        std::iota(label.begin(), label.end(), 0);
        self().shuffle(label);
        auto t = prufer_tree(n);
        for (auto& [u, v] : t) u = label[u], v = label[v];
        return t;
    }

    // raw に辺を足して m 本にする。ordered なら (u, v) と (v, u) を別の辺とみなす。
    // draw() はランダムな候補を 1 本返す。each(emit) は全候補を emit(u, v) で列挙する。
    template <class Draw, class Each>
    void fill_edges(std::vector<std::pair<ll, ll>>& raw, ll m, ll max_edges, bool simple, bool ordered, Draw draw,
                    Each each) {
        std::set<std::pair<ll, ll>> used;
        auto key = [&](ll u, ll v) { return ordered ? std::pair{u, v} : std::pair{std::min(u, v), std::max(u, v)}; };
        if (simple)
            for (auto [u, v] : raw) used.insert(key(u, v));
        const ll need = m - static_cast<ll>(raw.size());
        if (need <= 0) return;
        if (simple && need > (max_edges - static_cast<ll>(raw.size())) / 2) {
            // 密なとき: 候補を全列挙して選ぶ
            std::vector<std::pair<ll, ll>> cand;
            each([&](ll u, ll v) {
                if (!used.count(key(u, v))) cand.push_back({u, v});
            });
            self().shuffle(cand);
            cand.resize(need);
            raw.insert(raw.end(), cand.begin(), cand.end());
            return;
        }
        while (static_cast<ll>(raw.size()) < m) {
            auto [u, v] = draw();
            if (simple && !used.insert(key(u, v)).second) continue;
            raw.push_back({u, v});
        }
    }
    std::vector<std::pair<ll, ll>> prufer_tree(ll n) {
        std::vector<std::pair<ll, ll>> edges;
        if (n == 1) return edges;
        if (n == 2) return {{0, 1}};
        std::vector<ll> code(n - 2);
        for (auto& c : code) c = self().uniform(0, n - 1);
        std::vector<ll> degree(n, 1);
        for (ll c : code) ++degree[c];
        ll ptr = 0;
        while (degree[ptr] != 1) ++ptr;
        ll leaf = ptr;
        for (ll c : code) {
            edges.push_back({leaf, c});
            if (--degree[c] == 1 && c < ptr) {
                leaf = c;
            } else {
                ++ptr;
                while (degree[ptr] != 1) ++ptr;
                leaf = ptr;
            }
        }
        edges.push_back({leaf, n - 1});
        return edges;
    }

    // root >= 0 なら、添字 root の頂点を番号 base()（1 始まりなら 1）にする。
    // 無向のとき、ordered なら各辺を u <= v に、そうでなければ向きをランダムにする。
    Edges finish_edges(ll n, std::vector<std::pair<ll, ll>>& raw, const Value& w, bool shuffle_labels, bool directed,
                       ll root = -1, bool ordered = false) {
        std::vector<ll> label(n);
        std::iota(label.begin(), label.end(), 0);
        if (shuffle_labels) self().shuffle(label);
        if (root >= 0) std::swap(label[root], *std::find(label.begin(), label.end(), 0));
        self().shuffle(raw);
        Edges es;
        es.weighted = !std::holds_alternative<std::monostate>(w);
        es.reserve(raw.size());
        for (auto [u, v] : raw) {
            if (!directed && (ordered ? label[u] > label[v] : self().coin())) std::swap(u, v);
            Edge e{label[u] + self().base(), label[v] + self().base(), 0};
            if (es.weighted) e.w = self().draw(w);
            es.push_back(e);
        }
        return es;
    }
};

}  // namespace stress
