#include "stress/gen.hpp"
// n == 2 で gen error になる生成器（GEN_FAIL になる）
void gen(Gen& g) {
    int n = g.size(1, 30);
    if (n == 2) g.distinct(5, 1, 3);
    g.out(n);
    g.out(g.vec(n, 1, 10));
}
