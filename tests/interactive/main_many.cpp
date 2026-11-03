#include <bits/stdc++.h>
using namespace std;
int main() {  // 1 から順に聞く（回数制限を超える）
    int n;
    cin >> n;
    for (int y = 1; y < n; y++) {
        cout << "? " << y << endl;
        string r;
        cin >> r;
        if (r == "Yes") { cout << "! " << y << endl; return 0; }
    }
    cout << "! " << n << endl;
}
