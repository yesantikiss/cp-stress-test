#include "stress/gen.hpp"

void gen(Gen& g) {
    int n = g.i(1, 8);
    g.out(n, g.i(0, 1000000000));  // n と seed
}
