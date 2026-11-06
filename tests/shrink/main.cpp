#include <bits/stdc++.h>
// n >= 6 で 1 多く出す（縮小しても cap 6 より小さくはならない）
int main() { int n; std::cin >> n; long long s = 0; for (int i = 0; i < n; i++) { long long x; std::cin >> x; s += x; } if (n >= 6) s++; std::cout << s << "\n"; }
