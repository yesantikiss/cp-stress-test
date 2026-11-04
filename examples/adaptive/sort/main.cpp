#include <bits/stdc++.h>
using namespace std;

int main() {
    int n;
    cin >> n;
    auto less_than = [](int a, int b) {
        cout << "? " << a << ' ' << b << endl;
        string r;
        cin >> r;
        return r == "Yes";
    };
    // 二分挿入ソート。n = 8 でも比較は 17 回以下
    vector<int> order;  // p の値が小さい順に並べた添字
    for (int i = 1; i <= n; ++i) {
        int lo = 0, hi = order.size();
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (less_than(i, order[mid])) hi = mid;
            else lo = mid + 1;
        }
        order.insert(order.begin() + lo, i);
    }
    vector<int> p(n + 1);
    for (int k = 0; k < n; ++k) p[order[k]] = k + 1;
    cout << "!";
    for (int i = 1; i <= n; ++i) cout << ' ' << p[i];
    cout << endl;
}
