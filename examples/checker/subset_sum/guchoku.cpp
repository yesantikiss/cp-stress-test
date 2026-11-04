#include <bits/stdc++.h>
using namespace std;

// 部分集合を全部試す
int main() {
    int n;
    long long s;
    cin >> n >> s;
    vector<long long> a(n);
    for (auto& x : a) cin >> x;
    for (int mask = 1; mask < (1 << n); mask++) {
        long long sum = 0;
        for (int i = 0; i < n; i++)
            if (mask >> i & 1) sum += a[i];
        if (sum != s) continue;
        cout << "Yes\n" << __builtin_popcount(mask) << "\n";
        for (int i = 0; i < n; i++)
            if (mask >> i & 1) cout << i + 1 << " ";
        cout << "\n";
        return 0;
    }
    cout << "No\n";
}
