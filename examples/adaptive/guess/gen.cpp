#include "stress/gen.hpp"

void gen(Gen& g) {
    int n = g.size(1, 1000000);
    g.out(n, g.i(0, 1000000000));  // n と、interactor が同数の返事を選ぶための seed
}
