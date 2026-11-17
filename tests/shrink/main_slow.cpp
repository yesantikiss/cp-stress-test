#include <bits/stdc++.h>
// main.cpp と同じ答えで、1 回ごとに 30 ms 待つ。縮小中に Ctrl+C を送るテスト用（-j 4 でも縮小が 1 秒より長くかかるように）
int main() { std::this_thread::sleep_for(std::chrono::milliseconds(30)); int n; std::cin >> n; long long s = 0; for (int i = 0; i < n; i++) { long long x; std::cin >> x; s += x; } if (n >= 6) s++; std::cout << s << "\n"; }
