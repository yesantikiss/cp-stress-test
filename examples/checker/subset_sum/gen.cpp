#include "stress/gen.hpp"

void gen(Gen& g) {
    int n = g.size(1, 10);
    g.out(n, g.i(1, 30));
    g.out(g.vec(n, 1, 10));
}
