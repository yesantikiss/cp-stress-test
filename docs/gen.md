# gen.cpp

`stress/gen.hpp` を include して生成器を書く。中身は `stress/gen/` に分かれている: `core.hpp`（乱数）、`array.hpp`、`graph.hpp`（木とグラフ）、`string.hpp`、`output.hpp`。gen.cpp からは `stress/gen.hpp` だけ include すればいい。

```cpp
#include "stress/gen.hpp"

STRESS_GEN(random) {
    int n = g.size(1, 8);          // サイズは g.size（ドライバの cap で切られる）
    g.out(n);
    g.out(g.vec(n, 1, 10));        // g.i(l, r) は閉区間、1/8 で端の値が出る
}

STRESS_GEN(all_same) {             // コーナーケース用の生成器を名前付きで足せる
    int n = g.size(1, 8);
    g.out(n);
    g.out(g.vec(n, Range{5, 5}));
}

STRESS_BIG(max) {                  // 最大ケース。st では使わず、st bench / st make 用
    g.out(200000);
    g.out(g.vec(200000, 1, 1000000000));
}
```

`st` は STRESS_GEN を順番に混ぜて回し、失敗したらどの生成器かも出す。生成器が 1 つでいいなら、今まで通り `void gen(Gen& g) { ... }` と書いてもいい（名前は `gen` になる）。

| 関数 | 内容 |
| --- | --- |
| `i(l, r)` `uniform(l, r)` `size(l, r)` `real(l, r)` `coin(p)` | 乱数 |
| `vec(n, l, r)` `vec(n, Range{l, r})` `vec(n, [&]{...})` `sorted` `distinct` `perm` `partition(sum, k)` | 配列 |
| `choice(v)` `shuffle(v)` | 選択 |
| `tree(n, {.shape, .w})` | 木（random / path / star / caterpillar / binary / deep、省略時は混ぜる） |
| `graph(n, m, {.connected, .simple, .directed, .w})` | グラフ |
| `str(n, "ab", {.shape})` `str(n, [&]{...})` `mutate(s, k)` `strs(k, total, "ab")` `grid(h, w, ".#")` | 文字列（random / periodic / palindrome / fibonacci / runs） |
| `out(...)` `out(edges)` `out_lines(v)` | 出力 |

重み `.w` は `Range{l, r}` か、値を返すラムダ。頂点番号は既定で 1 始まり（`g.one_indexed = false` で 0 始まり）。
