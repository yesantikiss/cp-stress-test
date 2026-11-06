#include <bits/stdc++.h>
using namespace std;
// 範囲外の質問をして、返事が読めなければ abort する。
// interactor の WA で入力が閉じた直後に main が落ちるので、終わった順番では WA と RE が入れ替わりうる。
// 判定はいつも WA になるべき。
int main() {
    int n;
    cin >> n;
    cout << "? 0" << endl;
    string r;
    if (!(cin >> r)) abort();
    cout << "! 1" << endl;
}
