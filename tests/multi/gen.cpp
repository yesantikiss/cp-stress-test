#include "stress/gen.hpp"
// 総和の問題。名前付きの生成器を 3 つ
STRESS_GEN(random) {
    int n = g.size(1, 30);
    g.out(n);
    g.out(g.vec(n, 1, 10));
}
STRESS_GEN(all_same) {
    int n = g.size(1, 30);
    g.tag("same");
    g.out(n);
    g.out(g.vec(n, Range{7, 7}));
}
STRESS_BIG(max) {
    int n = 200000;
    g.out(n);
    g.out(g.vec(n, 1, 1000000000));
}
