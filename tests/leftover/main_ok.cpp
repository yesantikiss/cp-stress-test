#include <bits/stdc++.h>
int main() { std::ios::sync_with_stdio(false); std::cin.tie(nullptr); int t; std::cin >> t; while (t--) { int n; std::cin >> n; std::vector<long long> a(n); for (auto& x : a) std::cin >> x; std::cout << std::accumulate(a.begin(), a.end(), 0LL) << "\n"; } }
