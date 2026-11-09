#include "stress/gen.hpp"
// 深さ n の再帰を試すための入力。n は 100 万で固定。
STRESS_GEN(deep) { g.out(1000000); }
