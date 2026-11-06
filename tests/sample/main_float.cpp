#include <bits/stdc++.h>
// 総和に 1e-7 の誤差を足して小数で出す（--cmp float:1e-6 なら通り、token や float:1e-12 では落ちる）
int main() { int n; std::cin >> n; long long s = 0; for (int i = 0; i < n; i++) { long long a; std::cin >> a; s += a; } std::printf("%.9f\n", s + 1e-7); }
