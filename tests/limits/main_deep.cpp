#include <bits/stdc++.h>
// 深さ n の再帰。1 段あたり 100 バイト以上使うので、スタック 8 MB では落ちる。
long long f(long long n) {
    volatile char pad[100];
    pad[0] = static_cast<char>(n);
    if (n == 0) return pad[0];
    return f(n - 1) + 1 + pad[0] - static_cast<char>(n);
}
int main() { long long n; std::cin >> n; std::cout << f(n) << "\n"; }
