#pragma once

// 木とグラフ: tree / graph

#include "core.hpp"

namespace stress {

enum class TreeShape { mixed, random, path, star, caterpillar, binary, deep };

struct Edge {
    ll u, v, w;
};

struct Edges : std::vector<Edge> {
    bool weighted = false;
};

struct TreeOpt {
    TreeShape shape = TreeShape::mixed;
    Value w{};
};

struct GraphOpt {
    bool connected = false;
    bool simple = true;     // 自己ループと多重辺なし
    bool directed = false;
    Value w{};
};

template <class G>
class GraphOps {
    G& self() { return static_cast<G&>(*this); }

  public:

    Edges tree(ll n, TreeOpt opt = {}) {
        if (n <= 0) gen_error("tree: n must be positive");
        TreeShape shape = opt.shape;
        if (shape == TreeShape::mixed) {
            static constexpr TreeShape all[] = {TreeShape::random, TreeShape::random, TreeShape::path,
                                                TreeShape::star, TreeShape::caterpillar, TreeShape::binary,
                                                TreeShape::deep};
            shape = all[self().uniform(0, std::size(all) - 1)];
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
        return finish_edges(n, raw, opt.w, /*shuffle_labels=*/true, /*directed=*/false);
    }

    Edges graph(ll n, ll m, GraphOpt opt = {}) {
        if (n <= 0) gen_error("graph: n must be positive");
        const ll max_edges = opt.directed ? n * (n - 1) : n * (n - 1) / 2;
        if (opt.simple && m > max_edges)
            gen_error("graph: m=" + std::to_string(m) + " exceeds the maximum " + std::to_string(max_edges) + " for a simple graph");
        if (opt.connected && m < n - 1)
            gen_error("graph: connected graph needs m >= n-1");

        std::vector<std::pair<ll, ll>> raw;
        raw.reserve(m);
        std::set<std::pair<ll, ll>> used;
        auto key = [&](ll u, ll v) { return opt.directed ? std::pair{u, v} : std::pair{std::min(u, v), std::max(u, v)}; };
        auto add = [&](ll u, ll v) {
            if (opt.simple) {
                if (u == v || !used.insert(key(u, v)).second) return false;
            }
            raw.push_back({u, v});
            return true;
        };
        if (opt.connected) {
            Edges t = tree(n, {.shape = TreeShape::random});
            for (auto& e : t) {
                ll u = e.u - self().base(), v = e.v - self().base();
                if (opt.directed && self().coin()) std::swap(u, v);
                add(u, v);
            }
        }
        if (opt.simple && m - static_cast<ll>(raw.size()) > (max_edges - static_cast<ll>(raw.size())) / 2) {
            // 密なとき: 候補を全列挙して選ぶ
            std::vector<std::pair<ll, ll>> cand;
            for (ll u = 0; u < n; ++u)
                for (ll v = opt.directed ? 0 : u + 1; v < n; ++v)
                    if (u != v && !used.count(key(u, v))) cand.push_back({u, v});
            self().shuffle(cand);
            for (ll k = 0; static_cast<ll>(raw.size()) < m; ++k) add(cand[k].first, cand[k].second);
        } else {
            while (static_cast<ll>(raw.size()) < m) add(self().uniform(0, n - 1), self().uniform(0, n - 1));
        }
        return finish_edges(n, raw, opt.w, /*shuffle_labels=*/true, opt.directed);
    }

  private:
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

    Edges finish_edges(ll n, std::vector<std::pair<ll, ll>>& raw, const Value& w, bool shuffle_labels, bool directed) {
        std::vector<ll> label(n);
        std::iota(label.begin(), label.end(), 0);
        if (shuffle_labels) self().shuffle(label);
        self().shuffle(raw);
        Edges es;
        es.weighted = !std::holds_alternative<std::monostate>(w);
        es.reserve(raw.size());
        for (auto [u, v] : raw) {
            if (!directed && self().coin()) std::swap(u, v);
            Edge e{label[u] + self().base(), label[v] + self().base(), 0};
            if (es.weighted) e.w = self().draw(w);
            es.push_back(e);
        }
        return es;
    }
};

}  // namespace stress
