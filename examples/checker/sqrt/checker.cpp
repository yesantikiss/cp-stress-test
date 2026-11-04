#include <algorithm>
#include <cmath>
#include "stress/judge.hpp"

// x^2 = a となる x を 1 つ出力する（絶対誤差・相対誤差 1e-6 まで）
void judge(Judge& j) {
    double a = j.in.read_int();
    double x = j.out.read_double();
    j.ensure(std::abs(x * x - a) <= 1e-6 * std::max(1.0, a), "x^2 is too far from a");
    j.out.expect_eof();
}
