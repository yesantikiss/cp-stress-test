#include <bits/stdc++.h>
using namespace std;
int main() {
    int n;
    cin >> n;
    cout << "? 1" << endl;
    string r;
    cin >> r;
    if (n >= 4) abort();
    int lo = 1, hi = n;
    if (r == "Yes") hi = 1; else lo = 2;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        cout << "? " << mid << endl;
        cin >> r;
        if (r == "Yes") hi = mid; else lo = mid + 1;
    }
    cout << "! " << lo << endl;
}
