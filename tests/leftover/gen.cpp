#include "stress/gen.hpp"
// 複数テストケースの形式（先頭に T = 1）。main.cpp / guchoku.cpp は T を読まないので、T を n と読み違える
void gen(Gen& g) {
    int n = g.size(2, 10);
    g.out(1);
    g.out(n);
    g.out(g.vec(n, 1, 10));
}
