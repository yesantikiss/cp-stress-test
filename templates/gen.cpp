// st 用のジェネレータ。使える関数は ~/cp-stress-test/stress/gen.hpp を参照。
//
// STRESS_GEN(名前) を何個でも書ける。st はこれを順番に混ぜて回す（-g 名前 で絞れる）。
// STRESS_BIG(名前) は最大ケース用。st では使わず、st bench / st make で使う。
#include "stress/gen.hpp"

STRESS_GEN(random) {
    // サイズは g.size(lo, hi) で決める（小さいケースから順に試される）
    int n = g.size(1, 8);
    g.out(n);
    g.out(g.vec(n, 1, 10));
}

STRESS_GEN(all_same) {
    int n = g.size(1, 8);
    g.out(n);
    g.out(g.vec(n, Range{5, 5}));
}

STRESS_BIG(max) {
    int n = 200000;
    g.out(n);
    g.out(g.vec(n, 1, 1000000000));
}

// 例:
//   auto es = g.tree(n, {.w = Range{1, 100}});            g.out(es);
//   auto es = g.graph(n, m, {.connected = true});         g.out(es);
//   auto s  = g.str(n, "ab");                              g.out(s);
//   auto grid = g.grid(h, w, ".#");                        g.out_lines(grid);
