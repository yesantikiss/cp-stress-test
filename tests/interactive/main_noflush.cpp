#include <bits/stdc++.h>
using namespace std;
int main() {  // 高速化の定番 2 行を入れたまま '\n' で出す（flush されない）
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    int n;
    cin >> n;
    int lo = 1, hi = n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        cout << "? " << mid << '\n';
        string r;
        cin >> r;
        if (r == "Yes") hi = mid; else lo = mid + 1;
    }
    cout << "! " << lo << '\n';
}
