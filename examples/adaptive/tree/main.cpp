#include <bits/stdc++.h>
using namespace std;

int main() {
    int n;
    cin >> n;
    auto ask = [](int u, int v) {
        cout << "? " << u << ' ' << v << endl;
        int d;
        cin >> d;
        return d;
    };
    // 頂点 1 からの距離（深さ）を聞き、深さが 1 小さい頂点の中から親を探す
    vector<int> depth(n + 1, 0);
    for (int v = 2; v <= n; ++v) depth[v] = ask(1, v);
    vector<pair<int, int>> edges;
    for (int v = 2; v <= n; ++v) {
        vector<int> cand;
        for (int u = 1; u <= n; ++u)
            if (depth[u] == depth[v] - 1) cand.push_back(u);
        int parent = cand.back();  // 最後の 1 個は聞かなくても決まる
        for (int k = 0; k + 1 < (int)cand.size(); ++k)
            if (ask(cand[k], v) == 1) {
                parent = cand[k];
                break;
            }
        edges.emplace_back(parent, v);
    }
    cout << "!";
    for (auto [u, v] : edges) cout << ' ' << u << ' ' << v;
    cout << endl;
}
