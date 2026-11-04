#include <bits/stdc++.h>
using namespace std;

// 部分集合を全部試す
int main() {
    int n;
    cin >> n;
    vector<long long> a(n);
    for (auto& x : a) cin >> x;
    int best = 0, best_mask = 0;
    for (int mask = 1; mask < (1 << n); mask++) {
        long long last = LLONG_MIN;
        bool ok = true;
        for (int i = 0; i < n; i++)
            if (mask >> i & 1) {
                if (a[i] <= last) ok = false;
                last = a[i];
            }
        if (ok && __builtin_popcount(mask) > best) best = __builtin_popcount(mask), best_mask = mask;
    }
    cout << best << "\n";
    for (int i = 0; i < n; i++)
        if (best_mask >> i & 1) cout << i + 1 << " ";
    cout << "\n";
}
