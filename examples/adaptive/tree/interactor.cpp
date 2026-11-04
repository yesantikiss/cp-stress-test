#include <queue>

#include "stress/interact.hpp"
#include "stress/adaptive.hpp"

using Tree = std::vector<std::pair<int, int>>;  // cand::trees の 1 個: 辺 (u, v)（u < v）を昇順に並べた列

int dist(const Tree& t, int n, int s, int goal) {
    std::vector<std::vector<int>> adj(n + 1);
    for (auto [u, v] : t) adj[u].push_back(v), adj[v].push_back(u);
    std::vector<int> d(n + 1, -1);
    std::queue<int> q;
    d[s] = 0;
    q.push(s);
    while (!q.empty()) {
        int x = q.front();
        q.pop();
        for (int y : adj[x])
            if (d[y] < 0) d[y] = d[x] + 1, q.push(y);
    }
    return d[goal];
}

void interact(Interactor& it) {
    int n;
    unsigned long long seed;
    it.in >> n >> seed;
    it.send(n);
    it.query_limit(2 * n);
    Adaptive ad(cand::trees(n), seed);
    while (true) {
        std::string t;
        it.out >> t;
        if (t == "?") {
            int u, v;
            it.out >> u >> v;
            it.ensure(1 <= u && u <= n && 1 <= v && v <= n, "bad query");
            it.count_query();
            int d = ad.reply([&](const Tree& tr) { return dist(tr, n, u, v); });
            it.send(d);
        } else if (t == "!") {
            Tree ans(n - 1);
            for (auto& [u, v] : ans) {
                it.out >> u >> v;
                if (u > v) std::swap(u, v);  // 候補と同じ形（u < v、昇順）にそろえる
            }
            std::sort(ans.begin(), ans.end());
            ad.ensure_all([&](const Tree& tr) { return tr == ans; }, "answer is not determined yet");
            return;
        } else {
            it.wrong("unknown command: " + t);
        }
    }
}
