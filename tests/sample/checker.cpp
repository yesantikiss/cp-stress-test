#include "stress/judge.hpp"
// 出力が入力の総和と一致するか
void judge(Judge& j) {
    long long n = j.in.read_int(), s = 0;
    for (long long i = 0; i < n; i++) s += j.in.read_int();
    long long x = j.out.read_int();
    j.ensure(x == s, "expected " + std::to_string(s) + ", got " + std::to_string(x));
    j.out.expect_eof();
}
