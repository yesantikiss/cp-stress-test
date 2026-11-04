#include <array>
#include <numeric>

#include "stress/interact.hpp"
#include "stress/adaptive.hpp"

// 返事でつながったコインの成分を、偶奇付き Union-Find で持つ。
// color(x) が同じなら同じ種類、違えば違う種類。cnt[r][c] は成分 r で色 c のコインの枚数。
struct Coins {
    std::vector<int> parent, parity;
    std::vector<std::array<int, 2>> cnt;

    explicit Coins(int n) : parent(n + 1), parity(n + 1, 0), cnt(n + 1, {1, 0}) {
        std::iota(parent.begin(), parent.end(), 0);
    }
    std::pair<int, int> find(int x) const {  // (根, 根から見た色)
        int c = 0;
        while (parent[x] != x) c ^= parity[x], x = parent[x];
        return {x, c};
    }
    // 「x と y は diff（0 = 同じ, 1 = 違う）」を加える。矛盾したら false。
    bool unite(int x, int y, int diff) {
        auto [rx, cx] = find(x);
        auto [ry, cy] = find(y);
        if (rx == ry) return (cx ^ cy) == diff;
        if (cnt[rx][0] + cnt[rx][1] < cnt[ry][0] + cnt[ry][1]) std::swap(rx, ry);
        int flip = cx ^ cy ^ diff;  // ry の色を rx から見るとき反転するか
        parent[ry] = rx;
        parity[ry] = flip;
        for (int c = 0; c < 2; ++c) cnt[rx][c ^ flip] += cnt[ry][c];
        return true;
    }
};

// 偽物の集合としてありうる数: 各成分で片方の色を全部偽物にして、合計が m になる選び方の数。
__int128 count_sets(const Coins& s, int n, int m) {
    std::vector<__int128> dp(m + 1, 0);
    dp[0] = 1;
    for (int r = 1; r <= n; ++r) {
        if (s.parent[r] != r) continue;
        std::vector<__int128> nd(m + 1, 0);
        for (int k = 0; k <= m; ++k)
            if (dp[k] != 0)
                for (int c = 0; c < 2; ++c)
                    if (k + s.cnt[r][c] <= m) nd[k + s.cnt[r][c]] += dp[k];
        dp = std::move(nd);
    }
    return dp[m];
}

std::string to_string(__int128 x) {  // x >= 0
    std::string s;
    do s += char('0' + x % 10), x /= 10;
    while (x > 0);
    return {s.rbegin(), s.rend()};
}

void interact(Interactor& it) {
    int n, m, q;
    unsigned long long seed;
    it.in >> n >> m >> q >> seed;
    it.send(n, m, q);
    it.query_limit(q);
    AdaptiveState st(Coins(n), seed);
    while (true) {
        std::string t;
        it.out >> t;
        if (t == "?") {
            int x, y;
            it.out >> x >> y;
            it.ensure(1 <= x && x <= n && 1 <= y && y <= n && x != y, "bad query");
            it.count_query();
            int r = st.reply({0, 1},
                             [&](Coins& s, int diff) { return s.unite(x, y, diff); },
                             [&](const Coins& s) { return count_sets(s, n, m); });
            it.send(r);
        } else if (t == "!") {
            std::vector<int> ans(m);
            for (auto& a : ans) it.out >> a;
            const Coins& s = st.state();
            std::vector<char> fake(n + 1, 0);
            for (int a : ans) {
                it.ensure(1 <= a && a <= n && !fake[a], "bad answer");
                fake[a] = 1;
            }
            // 答えが矛盾しないか: どの成分でも、偽物と答えたのがちょうど片方の色全部
            std::vector<std::array<int, 2>> got(n + 1, {0, 0});
            for (int x = 1; x <= n; ++x) {
                auto [r, c] = s.find(x);
                if (fake[x]) ++got[r][c];
            }
            for (int r = 1; r <= n; ++r) {
                if (s.parent[r] != r) continue;
                bool ok = (got[r][0] == s.cnt[r][0] && got[r][1] == 0) || (got[r][0] == 0 && got[r][1] == s.cnt[r][1]);
                it.ensure(ok, "answer contradicts the replies");
            }
            // 矛盾しない集合がほかにもあれば、まだ決まっていない
            __int128 left = count_sets(s, n, m);
            it.ensure(left == 1, "answer is not determined yet (" +
                                     to_string(left) + " sets left)");
            return;
        } else {
            it.wrong("unknown command: " + t);
        }
    }
}
