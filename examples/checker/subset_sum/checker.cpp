#include <vector>
#include "stress/judge.hpp"

void judge(Judge& j) {
    int n = j.in.read_int();
    long long s = j.in.read_int();
    std::vector<long long> a(n);
    for (auto& x : a) x = j.in.read_int();

    bool exists = j.ref.read_word() == "Yes";
    std::string ans;
    j.out >> ans;
    if (ans == "No") {
        j.ensure(!exists, "answer exists but printed No");
    } else {
        j.ensure(ans == "Yes", "expected Yes or No, got " + ans);
        int k = j.out.read_int(1, n);
        std::vector<bool> used(n);
        long long sum = 0;
        for (int t = 0; t < k; t++) {
            int i = j.out.read_int(1, n) - 1;
            j.ensure(!used[i], "duplicate index");
            used[i] = true;
            sum += a[i];
        }
        j.ensure(sum == s, "sum is " + std::to_string(sum));
        if (!exists) j.fail("main found a subset but guchoku printed No");  // 愚直解のバグ
    }
    j.out.expect_eof();
}
