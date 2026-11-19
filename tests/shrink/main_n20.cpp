#include <bits/stdc++.h>
// n >= 20 で同じ値が 2 つ以上あるときだけ 1 多く出す。1 回ごとに 5 ms 待つので、
// 1 段階縮小だと cap 1..16 の 2000 回で時間切れになるが、2 段階縮小なら 1 回目で cap 20 まで縮む。
int main() {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    int n;
    std::cin >> n;
    std::vector<long long> a(n);
    long long s = 0;
    for (int i = 0; i < n; i++) {
        std::cin >> a[i];
        s += a[i];
    }
    bool dup = false;
    if (n >= 20) {
        std::sort(a.begin(), a.end());
        for (int i = 1; i < n; i++) {
            if (a[i] == a[i - 1]) dup = true;
        }
    }
    if (dup) s++;
    std::cout << s << "\n";
}
