#include <bits/stdc++.h>
using namespace std;

int main() {
    int n;
    cin >> n;
    int lo = 1, hi = n;  // x は [lo, hi] にある
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        cout << "? " << mid << endl;
        string r;
        cin >> r;
        if (r == "Yes") hi = mid;
        else lo = mid + 1;
    }
    cout << "! " << lo << endl;
}
