# cp-stress-test

競プロ用のランダムテスト（stress test）の道具。

- `stress/gen.hpp`: ジェネレータ用ヘッダ（seed 再現できる乱数、配列、木、グラフ、文字列、出力）
  - 中身は `stress/gen/` に分かれている: `core.hpp`（乱数）、`array.hpp`、`graph.hpp`（木とグラフ）、`string.hpp`、`output.hpp`。gen.cpp からは `stress/gen.hpp` だけ include すればいい。
- `stress/judge.hpp`: スペシャルジャッジ（checker）用ヘッダ
- `stress.py`: ドライバ（ビルド、TL 付き実行、比較、失敗の保存、サイズの縮小）

## 準備

```bash
# ~/.bashrc
alias st='python3 $HOME/cp-stress-test/stress.py'
```

## 使い方

問題のディレクトリ（`~/atcoder`）で:

```bash
st init            # gen.cpp の雛形を置く（-c で checker.cpp も）
st                 # main.cpp と guchoku.cpp を gen.cpp の入力で 1000 回比べる
st -n 5000 --seed 42
st -g random -g all_same  # 使う生成器を絞る
st -t 60           # 60 秒回す
st -c checker.cpp  # スペシャルジャッジ（--ref で愚直解の出力を第 3 引数に渡す）
st --tl 1 --san    # TL 1 秒、ASan / UBSan / _GLIBCXX_DEBUG
st --cmp float:1e-6
st repro SEED:CAP:NAME    # 失敗時に表示されたケースを再現
st make max -n 5 -o cases/  # 入力ファイルだけ書き出す
st bench max --tl 2       # 最大ケースで main の実行時間を測る（TLE なら終了コード 1）
st last --to-in    # 直前の失敗を表示して in.txt にコピー（元の in.txt は .stress/in.txt.bak）
st clean           # 保存した失敗を消す
```

終了コード: 0 = 全部 OK, 1 = 失敗を見つけた, 2 = ビルドや引数のエラー。

- ケース k は seed = splitmix64(base + k)、生成器は順番、サイズ上限 cap は生成器ごとに 1 から 10 ケースずつ増やす。小さい入力から試すので、最初に見つかる失敗はたいてい小さい。
- 失敗したら、もっと小さい cap で失敗しないか探し直してから保存する（`--no-shrink` で無効）。
- 失敗は `.stress/fail/<日時>-<種類>-<seed>/` に入力、両方の出力、stderr、meta.json として残る。`in.txt` には触らない。
- ビルドは `-std=gnu++20 -O2 -DLOCAL`、ACL と `$CP_LIBRARY_DIR`（既定 `~/cp-library`）を include パスに入れる。ACL を include していて `pch_acl.hpp.gch` があれば使う。依存ファイルが変わったときだけ再ビルドする。

## gen.cpp

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

## checker.cpp

```cpp
#include "stress/judge.hpp"

void judge(Judge& j) {
    long long n = j.in.read_int();
    long long x = j.out.read_int(1, n);   // 形式違反・範囲外は自動で WA
    j.ensure(x % 2 == 0, "x must be even");
    j.out.expect_eof();
}
```

`./checker <input> <output> [reference]`、終了コード 0 = AC, 1 = WA, 3 = checker の失敗。今までの `checker in out` 形式の checker もそのまま使える。

## テスト

```bash
bash tests/run_tests.sh
```
