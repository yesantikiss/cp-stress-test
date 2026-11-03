#include "stress/gen.hpp"
void gen(Gen& g) {
    int n = g.size(1, 30);
    g.out(n);
    g.out(g.vec(n, 1, 10));
}
