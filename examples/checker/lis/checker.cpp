#include <vector>
#include "stress/judge.hpp"

void judge(Judge& j) {
    int n = j.in.read_int();
    std::vector<long long> a(n);
    for (auto& x : a) x = j.in.read_int();

    long long best = j.ref.read_int();            // 愚直解の L だけ読む（添字は読まなくていい）
    long long len = j.out.read_int(0, n);
    j.ensure(len == best, "length " + std::to_string(len) + " is not optimal (" + std::to_string(best) + ")");
    int prev = 0;
    for (int k = 0; k < len; k++) {
        int i = j.out.read_int(1, n);
        j.ensure(prev < i, "indices must be increasing");
        j.ensure(prev == 0 || a[prev - 1] < a[i - 1], "not increasing");
        prev = i;
    }
    j.out.expect_eof();
}
