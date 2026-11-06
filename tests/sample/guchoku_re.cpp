#include <bits/stdc++.h>
// n == 2 で落ちる愚直解（REF_RE になる）
int main() { int n; std::cin >> n; if (n == 2) std::abort(); long long s = 0; for (int i = 0; i < n; i++) { long long a; std::cin >> a; s += a; } std::cout << s << "\n"; }
