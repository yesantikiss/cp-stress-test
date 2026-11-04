#include "stress/interact.hpp"
#include "stress/adaptive.hpp"

void interact(Interactor& it) {
    int n;
    unsigned long long seed;
    it.in >> n >> seed;
    it.send(n);
    it.query_limit(25);
    Adaptive ad(cand::range(1, n), seed);  // x としてありうる値を全部候補にする
    while (true) {
        std::string t;
        it.out >> t;
        if (t == "?") {
            int y;
            it.out >> y;
            it.ensure(1 <= y && y <= n, "y is out of range");
            it.count_query();
            bool yes = ad.reply([&](long long x) { return x <= y; });  // x が答えなら何と返すか
            it.send(yes ? "Yes" : "No");
        } else if (t == "!") {
            int ans;
            it.out >> ans;
            ad.ensure_all([&](long long x) { return x == ans; }, "answer is not determined yet");
            return;
        } else {
            it.wrong("unknown command: " + t);
        }
    }
}
