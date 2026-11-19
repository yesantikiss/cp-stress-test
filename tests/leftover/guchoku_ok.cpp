#include <cstdio>
int main() { int t; if (scanf("%d", &t) != 1) return 1; while (t--) { int n; if (scanf("%d", &n) != 1) return 1; long long s = 0; for (int i = 0; i < n; i++) { long long x; if (scanf("%lld", &x) != 1) return 1; s += x; } printf("%lld\n", s); } }
