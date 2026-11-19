// sync_with_stdio(false) のまま scanf で読み、exit で終わる
#include <bits/stdc++.h>
int main() { std::ios::sync_with_stdio(false); int n; if (scanf("%d", &n) != 1) return 1; for (int i = 0; i < n; i++) { int x; if (scanf("%d", &x) != 1) return 1; } printf("%d\n", n); std::exit(0); }
