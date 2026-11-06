#include "stress/gen.hpp"
// n が小さいときだけ、まれに gen error になる生成器。
// 本体のループ（cap 1 は 10 ケース）では出ないが、縮小（cap ごとに 200 回）では出る。
STRESS_GEN(random) {
    int n = g.size(1, 10);
    if (n <= 2 && g.coin(0.02)) g.distinct(5, 1, 3);
    g.out(n);
    g.out(g.vec(n, 1, 100));
}
