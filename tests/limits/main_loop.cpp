// 終わらない（Ctrl+C のあとに残らないかを見る）
int main() { volatile unsigned long x = 0; for (;;) x = x + 1; }
