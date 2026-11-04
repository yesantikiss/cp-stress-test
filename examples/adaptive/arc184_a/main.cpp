#include <bits/stdc++.h>
using namespace std;

// 19 枚と 21 枚の組を 25 個ずつ作り（計 1000 枚）、組の中で隣どうしを聞いて塗り分ける（計 950 回）。
// 組の大きさが奇数なので、どの組も 2 色の枚数が違う。
// - 21 枚の組: 多い方は 11 枚以上なので本物、少ない方が偽物
// - 19 枚の組: 多い方が 10 枚ちょうどのときだけ、その色が全部偽物（ほかは全部本物）の可能性がある
// 「全部の組で少ない方が偽物」か「1 つの組だけ多い方が偽物」のうち、合計が M になるものが 1 つだけある。
int main() {
    int n, m, q;
    cin >> n >> m >> q;
    vector<vector<int>> groups;
    int next = 1;
    for (int k = 0; k < 50; ++k) {
        int size = k < 25 ? 19 : 21;
        vector<int> g;
        for (int i = 0; i < size; ++i) g.push_back(next++);
        groups.push_back(g);
    }
    vector<array<vector<int>, 2>> cls;  // 組ごとの 2 色
    for (auto& g : groups) {
        array<vector<int>, 2> c;
        int color = 0;
        c[0].push_back(g[0]);
        for (int i = 1; i < (int)g.size(); ++i) {
            cout << "? " << g[i - 1] << ' ' << g[i] << endl;
            int r;
            cin >> r;
            color ^= r;
            c[color].push_back(g[i]);
        }
        if (c[0].size() > c[1].size()) swap(c[0], c[1]);  // c[0] が少ない方
        cls.push_back(c);
    }
    int sum_small = 0;
    for (auto& c : cls) sum_small += c[0].size();
    vector<int> ans;
    if (sum_small == m) {
        for (auto& c : cls) ans.insert(ans.end(), c[0].begin(), c[0].end());
    } else {
        for (auto& c : cls)
            if ((int)c[1].size() + sum_small - (int)c[0].size() == m) {
                ans = c[1];
                break;
            }
    }
    cout << "!";
    for (int a : ans) cout << ' ' << a;
    cout << endl;
}
