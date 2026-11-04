#include "stress/gen.hpp"

void gen(Gen& g) {
    int n = g.size(1, 8);
    g.out(n);
    g.out(g.vec(n, 1, 5));  // 値の幅を狭くして、同じ値が並ぶ場合を多くする
}
