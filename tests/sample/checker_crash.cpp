#include <vector>
#include "stress/judge.hpp"
// n == 2 で範囲外アクセスの例外を投げて落ちる checker（CHECKER_FAIL になる。main の WA ではない）
void judge(Judge& j) {
    long long n = j.in.read_int(), s = 0;
    std::vector<long long> a(n);
    for (auto& x : a) s += x = j.in.read_int();
    if (n == 2) s += a.at(5);
    j.ensure(j.out.read_int() == s, "wrong sum");
}
