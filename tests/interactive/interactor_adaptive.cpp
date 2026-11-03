// 適応的（adaptive）な interactor の例。
// x を最初に決めず、「まだ x としてありうる範囲 [lo, hi]」だけを持つ。
// 質問には、ありうる範囲が広く残る方を答える（解答に一番不利な答え方）。
// 答えが当たりになるのは、範囲が 1 つに絞れていてそれと一致したときだけ。
#include "stress/interact.hpp"

void interact(Interactor& it) {
    int n;
    unsigned long long seed;
    it.in >> n >> seed;  // 同じ広さのときの選び方に使う。入力に入れておけば再現できる
    it.send(n);
    it.query_limit(25);
    int lo = 1, hi = n;
    while (true) {
        std::string t;
        it.out >> t;
        if (t == "?") {
            int y;
            it.out >> y;
            it.ensure(1 <= y && y <= n, "y is out of range");
            it.count_query();
            // "Yes"（x <= y）なら [lo, min(hi, y)]、"No" なら [max(lo, y + 1), hi] が残る
            long long yes = std::max(0, std::min(hi, y) - lo + 1);
            long long no = std::max(0, hi - std::max(lo, y + 1) + 1);
            bool say_yes = yes != no ? yes > no : (seed >> (it.queries() % 64)) & 1;
            if (say_yes) hi = std::min(hi, y);
            else lo = std::max(lo, y + 1);
            it.send(say_yes ? "Yes" : "No");
        } else if (t == "!") {
            int ans;
            it.out >> ans;
            it.ensure(lo == hi && ans == lo,
                      "answer " + std::to_string(ans) + " is not determined yet (x can be in [" +
                          std::to_string(lo) + ", " + std::to_string(hi) + "])");
            return;
        } else {
            it.wrong("unknown command: " + t);
        }
    }
}
