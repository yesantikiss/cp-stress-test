#include <bits/stdc++.h>
using namespace std;

// O(n log n) の最長（狭義）増加部分列。長さと添字（1-indexed）を出力する
int main() {
    int n;
    cin >> n;
    vector<long long> a(n);
    for (auto& x : a) cin >> x;
    vector<long long> tail;
    vector<int> tail_id, prv(n, -1);
    for (int i = 0; i < n; i++) {
        int k = lower_bound(tail.begin(), tail.end(), a[i]) - tail.begin();
        if (k == (int)tail.size()) tail.push_back(a[i]), tail_id.push_back(i);
        else tail[k] = a[i], tail_id[k] = i;
        prv[i] = k ? tail_id[k - 1] : -1;
    }
    vector<int> idx;
    for (int i = tail_id.back(); i != -1; i = prv[i]) idx.push_back(i + 1);
    reverse(idx.begin(), idx.end());
    cout << idx.size() << "\n";
    for (int i = 0; i < (int)idx.size(); i++) cout << idx[i] << " \n"[i + 1 == (int)idx.size()];
}
