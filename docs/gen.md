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
| `dag(n, m, {.connected, .simple, .increasing, .w})` | DAG |
| `bipartite(n1, n2, m, {.connected, .simple, .merged, .w})` `bipartite(n, m, {...})` | 二部グラフ |
| `str(n, "ab", {.shape})` `str(n, [&]{...})` `mutate(s, k)` `strs(k, total, "ab")` `grid(h, w, ".#")` | 文字列（random / periodic / palindrome / fibonacci / runs） |
| `out(...)` `out(edges)` `out_lines(v)` | 出力 |

重み `.w` は `Range{l, r}` か、値を返すラムダ。頂点番号は既定で 1 始まり（`g.one_indexed = false` で 0 始まり）。

以下、それぞれの関数を詳しく説明する。

## 乱数の仕組み

- 乱数は xoshiro256\*\*。seed を splitmix64 で 4 回まわして内部状態を作る。
- `std::uniform_int_distribution` などの std の分布は使っていないので、seed が同じならコンパイラや環境が違っても同じ入力が出る。
- gen は `./gen <seed> [cap] [name]` で 1 ケース出す。`cap` に `-` を渡すと上限なし。`name` を省略すると先頭の生成器を使う。`./gen --list` で生成器の名前が出る（STRESS_BIG は名前の後に ` big`）。
- 引数がおかしいとき（`lo > hi` など）は `[gen error] ...` を stderr に出して終了コード 3 で終わる。

## 整数の乱数

| 関数 | 範囲 | 分布 |
| --- | --- | --- |
| `g.uniform(lo, hi)` | 閉区間 `[lo, hi]` | 一様（偏りなし） |
| `g.i(lo, hi)` `g.i(Range{lo, hi})` | 閉区間 `[lo, hi]` | 1/8 の確率で端の値、残りは一様 |

`i` は端の値を出やすくしてある。1/8 の確率で `lo`、`lo + 1`、`hi - 1`、`hi` のどれかを等確率で返し、残りの 7/8 で `uniform(lo, hi)` を返す。たとえば `i(1, 1000000000)` では `1` や `1000000000` がだいたい 1/32 の確率で出る。境界のバグを見つけたいときは `i`、値の分布を本当に一様にしたいときは `uniform` を使う。

`lo == hi` ならその値を返す。`lo > hi` はエラー。

## サイズと cap

`g.size(lo, hi)` は N や M のような「入力の大きさ」を決めるための関数。上限を `min(hi, cap)` に切り詰めてから `i` で引く。ただし `lo` より小さくはしないので、実際の範囲は `[lo, max(lo, min(hi, cap))]` になる。

`cap` はドライバが渡す値で、`st` では小さい入力から試すために次のように増える。

- 生成器ごとに、その生成器の何ケース目か（k = 0, 1, 2, ...）で `cap = 1 + k / 10`。最初の 10 ケースは cap 1、次の 10 ケースは cap 2、...
- `--max-cap X` を付けると cap は X で止まる。
- `st make` / `st bench` / `./gen` を直接実行したときは、`--cap` で指定しない限り上限なし。

なので `int n = g.size(1, 8);` と書くと、最初の 10 ケースは必ず `n = 1` になり、71 ケース目（cap 8）から 1 から 8 まで全部出るようになる。`g.size(3, 8)` なら cap が 1 や 2 でも `n = 3` になる。

cap は `size` にしか効かない。`g.i(1, 8)` で N を決めると、最初から大きい入力が出るので、失敗したときに小さいケースが見つかりにくい。サイズは `size` で決めるのがいい。

## 実数と確率

- `g.real(lo, hi)`: 半開区間 `[lo, hi)` の一様な実数（53 bit 精度）。`i` のような端の偏りはない。
- `g.coin(p)`: 確率 `p` で `true`。`p` を省略すると 0.5。

## 選択と並べ替え

- `g.choice(v)`: `vector` から一様に 1 個選んで返す。`g.choice("abc")` のように文字列を渡すと 1 文字返す。空だとエラー。
- `g.shuffle(v)`: `vector` か `string` をその場でシャッフルする（Fisher–Yates、全順列が等確率）。

## 値の指定（Range とラムダ）

重み `.w` や `vec` の値には、範囲かラムダを渡せる。

```cpp
g.vec(n, Range{1, 10});                       // 各要素を g.i(1, 10) で引く
g.vec(n, [&] { return g.coin(0.9) ? 0 : g.i(1, 100); });  // 9 割が 0
g.tree(n, {.w = Range{1, 100}});
g.tree(n, {.w = [&] { return g.i(1, 5) * 2; }});          // 偶数の重み
```

`Range{l, r}` は「`g.i(l, r)` を返すラムダ」の省略形で、端の値が出やすいのも同じ。

## 配列

| 関数 | 返すもの |
| --- | --- |
| `g.vec(n, lo, hi)` `g.vec(n, Range{lo, hi})` | 長さ n、各要素を `g.i(lo, hi)` で独立に引く |
| `g.vec(n, f)` | 長さ n、各要素を `f()` で引く |
| `g.sorted(n, lo, hi)` | `vec(n, lo, hi)` を昇順にソートしたもの（重複あり） |
| `g.distinct(n, lo, hi)` | `[lo, hi]` から相異なる n 個、順番はランダム |
| `g.perm(n)` | 順列。1 始まりなら 1..n、`one_indexed = false` なら 0..n-1 |
| `g.partition(sum, k, min_value = 0)` | 和が `sum` になる k 個の整数（各要素は `min_value` 以上） |

注意点:

- `vec` と `sorted` は `i` を使うので、`lo` や `hi` が重複して出やすい。
- `distinct` は `uniform` を使うので端の偏りはない。範囲の中の値が n 個より少ないとエラー。
- `partition` は `sum - min_value * k` を k 個に分ける区切り位置を一様に引いて作る。0 を含みたくないときは `min_value = 1`。`sum < min_value * k` だとエラー。

## 木

```cpp
auto es = g.tree(n);                                        // 形は混ぜる
auto es = g.tree(n, {.shape = TreeShape::path});
auto es = g.tree(n, {.shape = TreeShape::star, .w = Range{1, 100}});
g.out(es);                                                  // 1 本 1 行で n-1 行
```

| `.shape` | 形 |
| --- | --- |
| `TreeShape::random` | Prüfer 列から作る一様ランダムな木 |
| `TreeShape::path` | パス（直線） |
| `TreeShape::star` | スター（1 頂点に全部つながる） |
| `TreeShape::caterpillar` | 長さ 1 から n/2 の背骨のパスに、残りの頂点を葉としてつける |
| `TreeShape::binary` | 完全二分木 |
| `TreeShape::deep` | 各頂点の親を直前の 1 から 3 頂点から選ぶ、深い木 |
| `TreeShape::mixed`（省略時） | 上のどれかを選ぶ（random が 2/7、他は 1/7 ずつ） |

どの形でも、頂点番号はランダムに振り直し、辺の順番と各辺の `u v` の向きもシャッフルする。なので path でも `1-2-3-...` の順には出ない。根を 1 にしたい問題では、自分で番号を付け直す必要がある。

## グラフ

```cpp
auto es = g.graph(n, m);                                    // 単純な無向グラフ
auto es = g.graph(n, m, {.connected = true, .w = Range{1, 1000000000}});
auto es = g.graph(n, m, {.simple = false, .directed = true});
```

| オプション | 既定 | 意味 |
| --- | --- | --- |
| `.connected` | `false` | 連結にする。先にランダムな全域木を入れてから残りの辺を足す。`m >= n - 1` が必要 |
| `.simple` | `true` | 自己ループと多重辺を作らない。`false` だとどちらも出る |
| `.directed` | `false` | 有向グラフ。`connected` と一緒に使うと、全域木の辺の向きはランダムなので強連結にはならない |
| `.w` | なし | 重み（Range かラムダ） |

単純グラフで `m` が辺の最大数（無向 n(n-1)/2、有向 n(n-1)）を超えるとエラー。辺の順番と頂点番号は木と同じくシャッフルされる。

## DAG

```cpp
auto es = g.dag(n, m);                                      // 単純な DAG
auto es = g.dag(n, m, {.connected = true});                 // 弱連結
auto es = g.dag(n, m, {.increasing = true});                // 常に u < v
```

ランダムなトポ順を作り、トポ順で前の頂点から後ろの頂点への辺だけを張る。頂点番号と辺の順番はシャッフルするので、出力からトポ順は分からない。

| オプション | 既定 | 意味 |
| --- | --- | --- |
| `.connected` | `false` | 弱連結にする（向きを無視して連結）。`m >= n - 1` が必要 |
| `.simple` | `true` | 多重辺を作らない。自己ループはどちらでも出ない |
| `.increasing` | `false` | 頂点番号をトポ順のままにして、常に `u < v` にする。「A_i < B_i」の制約がある問題向け |
| `.w` | なし | 重み（Range かラムダ） |

単純な DAG の辺の最大数は n(n-1)/2。

## 二部グラフ

```cpp
auto es = g.bipartite(n1, n2, m);                           // 左 n1 頂点、右 n2 頂点
auto es = g.bipartite(n1, n2, m, {.merged = true});         // n1+n2 頂点の通し番号
auto es = g.bipartite(n, m, {.connected = true});           // 総頂点数だけ指定
```

`bipartite(n1, n2, m)` は既定で左右に別々の番号を振る。各辺 `u v` の `u` が左（1 から n1）、`v` が右（1 から n2）で、マッチングの問題によくある入力形式になる。`.merged = true` にすると、n1+n2 頂点に通し番号をシャッフルして振り、各辺の向きもランダムにする。「二部グラフか判定せよ」のような、普通のグラフとして入力される問題向け。

`bipartite(n, m)` は総頂点数だけ指定する版で、左右の大きさは m 本の辺が入る分け方からランダムに選ぶ。番号は常に通し番号（`.merged` は無視される）。

| オプション | 既定 | 意味 |
| --- | --- | --- |
| `.connected` | `false` | 連結にする。先に二部グラフの全域木を入れてから残りの辺を足す。`m >= n1 + n2 - 1` と、両側が 1 頂点以上あることが必要 |
| `.simple` | `true` | 多重辺を作らない |
| `.merged` | `false` | 通し番号にする（上の説明） |
| `.w` | なし | 重み（Range かラムダ） |

単純な二部グラフの辺の最大数は n1 * n2。無向のみ。

## 辺に文字を付ける

重み `.w` は整数なので、文字を返すラムダを渡すと文字コードの数値が出る。辺に文字を付けたいときは、重みなしで作って自分で出す。

```cpp
for (auto e : g.tree(n)) g.out(e.u, e.v, g.choice("abc"));  // "u v c" を n-1 行
```

## 頂点番号

`g.one_indexed` が `true`（既定）なら `tree` / `graph` / `dag` / `bipartite` / `perm` は 1 始まり、`false` なら 0 始まりになる。生成器の最初で `g.one_indexed = false;` と書けばその生成器の中で全部 0 始まりになる。

## 文字列

```cpp
auto s = g.str(n, "ab");                                    // 形は混ぜる
auto s = g.str(n, "abc", {.shape = StrShape::palindrome});
auto s = g.str(n, [&] { return g.coin(0.1) ? 'b' : 'a'; });
```

`str(n, alphabet)` は alphabet の文字だけを使った長さ n の文字列を返す。

| `.shape` | 形 |
| --- | --- |
| `StrShape::random` | 各文字を一様に選ぶ |
| `StrShape::periodic` | 周期 p の繰り返し（p は半々の確率で 1 から min(n, 4)、または 1 から n/2）に、0 から 2 文字の書き換えを加える |
| `StrShape::palindrome` | 回文。半々の確率で全体が 1 つの回文、または 2 から 4 個の回文をつなげたもの |
| `StrShape::fibonacci` | 2 種類の文字で作るフィボナッチ文字列の一部分 |
| `StrShape::runs` | 同じ文字の連続（ラン）を並べたもの。隣のランは違う文字 |
| `StrShape::mixed`（省略時） | 上のどれかを選ぶ（random が 2/6、他は 1/6 ずつ） |

periodic は書き換えが入るので、ちょうど周期的とは限らない（周期的に近いケースを作るため）。alphabet が 1 文字なら、どの形でも同じ文字の繰り返しになる。

その他の関数:

- `g.mutate(s, k, alphabet)`: s のランダムな位置を k 回書き換えた文字列を返す。同じ位置や同じ文字を選ぶこともあるので、実際に変わる文字数は k 以下。alphabet を省略すると s に出てくる文字から選ぶ。
- `g.strs(k, total, alphabet, {.shape})`: k 個の文字列。長さの総和を `i(k, total)` で決めて、各長さが 1 以上になるように `partition` で分ける。`total < k` だとエラー。
- `g.grid(h, w, ".#")`: h 行 w 列のグリッド（`vector<string>`）。各マスを一様に選ぶ。ラムダも渡せる。

## 出力

| 関数 | 出力 |
| --- | --- |
| `g.out(a, b, ...)` | 引数を空白区切りで 1 行。`vector` は要素を空白区切りで展開する |
| `g.out(es)` | 辺のリストを 1 本 1 行。重み付きなら `u v w`、なければ `u v` |
| `g.out_lines(v)` | 1 要素 1 行（グリッドや文字列のリスト向け） |
| `g.raw(s)` | 改行なしでそのまま足す |

```cpp
g.out(n, m);          // "3 5"
g.out(a);             // "1 4 2"
g.out(n, a);          // "3 1 4 2"（1 行にまとまる）
g.out(s);             // 文字列もそのまま
g.out_lines(grid);
```

- `char` はそのまま 1 文字、`bool` は `1` / `0`、実数は値を表せる一番短い形（`std::to_chars`）で出る。
- `Edges` と `vector<string>` は他の値と並べて `out(...)` に入れられない（コンパイルエラーになる）。それぞれ `out(es)` と `out_lines(v)` で単独で出す。
- 出力はバッファにためて、生成器が終わったときにまとめて書き出す。
