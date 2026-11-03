#include "stress/gen.hpp"
void gen(Gen& g) {
    int n = g.size(1, 1000000);
    g.out(n, g.i(1, n));
}
