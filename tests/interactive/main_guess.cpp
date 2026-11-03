#include <bits/stdc++.h>
using namespace std;
int main() {  // 幅 2 まで絞ったら運に任せて hi を答える（固定の x なら半分通る）
    int n;
    cin >> n;
    int lo = 1, hi = n;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        cout << "? " << mid << endl;
        string r;
        cin >> r;
        if (r == "Yes") hi = mid; else lo = mid + 1;
    }
    cout << "! " << hi << endl;
}
