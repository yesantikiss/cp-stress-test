// st -c 用の checker。j.in は問題の入力、j.out は main の出力、
// j.ref は --ref を付けたときの愚直解の出力。
// 形式違反は自動で WA。j.wrong("理由") で WA、何事もなく戻れば AC。
#include "stress/judge.hpp"

void judge(Judge& j) {
    long long n = j.in.read_int();
    long long x = j.out.read_int(1, n);
    j.ensure(x >= 1, "x must be positive");
    j.out.expect_eof();
}
