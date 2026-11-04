#include <bits/stdc++.h>
using namespace std;

// 部分和 DP で、和が s になる部分集合を 1 つ復元する
int main() {
    int n;
    long long s;
    cin >> n >> s;
    vector<long long> a(n);
    for (auto& x : a) cin >> x;
    // dp[i][t]: 先頭 i 個で和 t を作れるか
    vector<vector<char>> dp(n + 1, vector<char>(s + 1));
    dp[0][0] = 1;
    for (int i = 0; i < n; i++)
        for (long long t = 0; t <= s; t++)
            if (dp[i][t]) {
                dp[i + 1][t] = 1;
                if (t + a[i] <= s) dp[i + 1][t + a[i]] = 1;
            }
    if (!dp[n][s]) {
        cout << "No\n";
        return 0;
    }
    vector<int> idx;
    for (int i = n; i > 0; i--)
        if (!dp[i - 1][s]) idx.push_back(i), s -= a[i - 1];
    cout << "Yes\n" << idx.size() << "\n";
    for (int i = 0; i < (int)idx.size(); i++) cout << idx[i] << " \n"[i + 1 == (int)idx.size()];
}
