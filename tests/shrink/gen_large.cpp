#include "stress/gen.hpp"
// 大きい cap まで進みやすい生成器（2 段階縮小のテスト用）
void gen(Gen& g) {
    int n = g.size(1, 100);
    g.out(n);
    g.out(g.vec(n, [&] { return g.uniform(1, 20000); }));
}
