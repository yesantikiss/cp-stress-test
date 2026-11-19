// T を読まずに n から読む（1 個読んで終わり、残りを読み残す）
#include <bits/stdc++.h>
int main() { int n; std::cin >> n; std::vector<long long> a(n); for (auto& x : a) std::cin >> x; std::cout << std::accumulate(a.begin(), a.end(), 0LL) << "\n"; }
