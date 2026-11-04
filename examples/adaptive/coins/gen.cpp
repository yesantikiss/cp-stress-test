#include "stress/gen.hpp"

void gen(Gen& g) {
    g.out(1000, 10, 950, g.i(0, 1000000000));  // N M Q と seed
}
