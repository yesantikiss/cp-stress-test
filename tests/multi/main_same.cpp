#include <bits/stdc++.h>
// 全部同じ値で n >= 3 のときだけ間違える
int main() { int n; std::cin >> n; std::vector<long long> a(n); for (auto& x : a) std::cin >> x; long long s = std::accumulate(a.begin(), a.end(), 0LL); if (n >= 3 && std::count(a.begin(), a.end(), a[0]) == n) s++; std::cout << s << "\n"; }
