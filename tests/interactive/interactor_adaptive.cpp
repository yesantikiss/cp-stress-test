// 適応的（adaptive）な interactor の例。
// x を最初に決めず、「x としてまだありうる値」を全部候補として持つ。
// 質問には、候補が一番多く残る返事をする（解答に一番不利な答え方）。
// 答えが当たりになるのは、候補が 1 つに絞れていてそれと一致したときだけ。
#include "stress/interact.hpp"
#include "stress/adaptive.hpp"

void interact(Interactor& it) {
    int n;
    unsigned long long seed;
    it.in >> n >> seed;  // 同数のときの選び方に使う。入力に入れておけば再現できる
    it.send(n);
    it.query_limit(25);
    Adaptive ad(cand::range(1, n), seed);
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
