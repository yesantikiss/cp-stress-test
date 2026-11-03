#include <bits/stdc++.h>
int main() { int n; std::cin >> n; std::vector<long long> a(n); for (auto& x : a) std::cin >> x; if (n == 4) std::abort(); std::cout << std::accumulate(a.begin(), a.end(), 0LL) << "\n"; }
