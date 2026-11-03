#include <bits/stdc++.h>
// O(n^2)
int main() { int n; std::cin >> n; std::vector<long long> a(n); for (auto& x : a) std::cin >> x; long long s = 0; for (int i = 0; i < n; i++) for (int j = 0; j <= i; j++) if (j == i) s += a[i]; std::cout << s << "\n"; }
