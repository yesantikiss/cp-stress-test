#include <bits/stdc++.h>
// n >= 6 で最後の要素を足し忘れる
int main() { int n; std::cin >> n; std::vector<long long> a(n); for (auto& x : a) std::cin >> x; long long s = 0; for (int i = 0; i < (n >= 6 ? n - 1 : n); i++) s += a[i]; std::cout << s << "\n"; }
