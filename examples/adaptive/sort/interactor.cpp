#include "stress/interact.hpp"
#include "stress/adaptive.hpp"

void interact(Interactor& it) {
    int n;
    unsigned long long seed;
    it.in >> n >> seed;
    it.send(n);
    it.query_limit(3 * n);
    Adaptive ad(cand::permutations(n), seed);
    while (true) {
        std::string t;
        it.out >> t;
        if (t == "?") {
            int a, b;
            it.out >> a >> b;
            it.ensure(1 <= a && a <= n && 1 <= b && b <= n && a != b, "bad query");
            it.count_query();
            // 列は 0 始まりなので p_a は p[a - 1]
            bool yes = ad.reply([&](const std::vector<int>& p) { return p[a - 1] < p[b - 1]; });
            it.send(yes ? "Yes" : "No");
        } else if (t == "!") {
            std::vector<int> ans(n);
            for (auto& v : ans) it.out >> v;
            ad.ensure_all([&](const std::vector<int>& p) { return p == ans; }, "answer is not determined yet");
            return;
        } else {
            it.wrong("unknown command: " + t);
        }
    }
}
