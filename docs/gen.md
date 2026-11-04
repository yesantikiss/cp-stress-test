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

`st` は STRESS_GEN を順番に混ぜて回し、失敗したらどの生成器かも出す。生成器が 1 つでいいなら、`void gen(Gen& g) { ... }` と書いてもいい（名前は `gen` になる）。

| 種類 | 関数 |
| --- | --- |
| [整数の乱数](#整数の乱数) | `i(l, r)` `uniform(l, r)` `size(l, r)` |
| [実数と確率](#実数と確率) | `real(l, r)` `decimal(l, r, d, {...})` `coin(p)` |
| [選択と並べ替え](#選択と並べ替え) | `choice(v)` `shuffle(v)` |
| [配列](#配列) | `vec(n, l, r)` `seq(n, l, r, {...})` `sorted(n, l, r, {...})` `distinct(n, l, r)` `perm(n)` `partition(sum, k)` |
| [木](#木) | `tree(n, {...})` `parents(n, {...})` |
| [グラフ](#グラフ) | `graph(n, m, {...})` `digraph(n, m, {...})` `dag(n, m, {...})` `bipartite(n1, n2, m, {...})` |
| [文字列](#文字列) | `str(n, "ab", {...})` `mutate(s, k)` `strs(k, total, "ab", {...})` `grid(h, w, ".#")` |
| [巨大整数](#巨大整数) | `big(len, {...})` `big_range("1", "1e100", {...})`（文字列で返す） |
| [出力](#出力) | `out(...)` `out(edges)` `out_lines(v)` `raw(s)` |
| [タグ](#タグ) | `tag("name")`（失敗時に表示される） |

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
| `g.size(lo, hi)` | `[lo, max(lo, min(hi, cap))]` | `i` と同じ（[サイズと cap](#サイズと-cap)） |

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

| 関数 | 返すもの |
| --- | --- |
| `g.real(lo, hi)` | 半開区間 `[lo, hi)` の一様な実数（53 bit 精度）。gen.cpp の中の計算用 |
| `g.decimal(lo, hi, d, {...})` | 閉区間 `[lo, hi]` の小数を文字列で返す。入力に小数を出すときはこちら |
| `g.coin(p)` | 確率 `p` で `true`。`p` を省略すると 0.5 |

```cpp
g.decimal(0, 100, 2);                    // 小数点以下 2 桁以下: "37" "4.5" "99.25" など
g.decimal(0, 100, 2, {.exact = true});   // ちょうど 2 桁: "37.00" "4.50" "99.25" など
g.decimal(-1.5, 1.5, 3);                 // 負の数もいい: "-0.125" "1.5" など
```

| `decimal` のオプション | 既定 | 意味 |
| --- | --- | --- |
| `.exact` | `false` | `true` なら小数点以下ちょうど d 桁 |

- `real`: `i` のような端の偏りはない。そのまま `out` すると `0.7234912873457213` のような最短表記になる。
- `decimal`: 既定は「小数点以下 d 桁以下」で、末尾の 0 は付けない（整数なら小数点も付けない）。先に桁数を 0〜d から一様に選んでから値を選ぶので、`37` や `4.5` のような短い形もよく出る。選んだ桁数の値が範囲に無いときは桁数を増やす。
- `decimal`: 値は `10^-d` 刻みで、`i` と同じく端が出やすい。`lo` `hi` は `10^-d` 刻みに丸める（`0.1` を `0.1000000000000000055…` として扱ったりはしない）。
- `decimal`: `d` は 0〜18。`|lo|` と `|hi|` に `10^d` を掛けたものは 9e18 未満。範囲に `10^-d` 刻みの値が無いとエラー。

## 選択と並べ替え

| 関数 | 動き |
| --- | --- |
| `g.choice(v)` | `vector` から一様に 1 個選んで返す。文字列を渡すと 1 文字返す。空だとエラー |
| `g.shuffle(v)` | `vector` か `string` をその場でシャッフルする（Fisher–Yates、全順列が等確率） |

## 値の指定（Range とラムダ）

重み `.w` や `vec` の値には、範囲かラムダを渡せる。

```cpp
g.vec(n, Range{1, 10});                       // 各要素を g.i(1, 10) で引く
g.vec(n, [&] { return g.coin(0.9) ? 0 : g.i(1, 100); });  // 9 割が 0
g.tree(n, {.w = Range{1, 100}});
g.tree(n, {.w = [&] { return g.i(1, 5) * 2; }});          // 偶数の重み
```

`Range{l, r}` は「`g.i(l, r)` を返すラムダ」の省略形で、端の値が出やすいのも同じ。

### ラムダの例

```cpp
// 小さい値と大きい値を半々
g.vec(n, [&] { return g.coin() ? g.i(1, 10) : g.i(999999990, 1000000000); });

// 桁数をばらけさせる（1 桁から 18 桁がだいたい同じ割合で出る）
g.vec(n, [&] {
    long long p = 1;
    for (int k = g.i(0, 17); k--;) p *= 10;
    return g.i(p, p * 10 - 1);
});

// 状態を持つラムダ。隣との差が -1, 0, 1 のどれかになる列（[1, 100] に収める）
long long cur = g.i(1, 100);
g.vec(n, [&] { return cur = std::clamp(cur + g.i(-1, 1), 1LL, 100LL); });

// 同じラムダを配列と重みで使い回す
auto val = [&] { return g.coin(0.2) ? 0 : g.i(1, 100); };
auto a = g.vec(n, val);
auto es = g.tree(n, {.w = val});

// グリッド。2 割が壁
auto grid = g.grid(h, w, [&] { return g.coin(0.2) ? '#' : '.'; });

// 関数オブジェクトも渡せる
struct Pow2 {
    Gen& g;
    long long operator()() const { return 1LL << g.i(0, 60); }
};
g.vec(n, Pow2{g});
g.graph(n, m, {.w = Pow2{g}});
```

## 配列

| 関数 | 返すもの |
| --- | --- |
| `g.vec(n, lo, hi)` `g.vec(n, Range{lo, hi})` | 長さ n、各要素を `g.i(lo, hi)` で独立に引く |
| `g.vec(n, f)` | 長さ n、各要素を `f()` で引く |
| `g.seq(n, lo, hi, {...})` | 長さ n、形を選んで作る配列（[seq の形](#seq-の形)） |
| `g.sorted(n, lo, hi, {...})` | 単調な列。既定は広義増加（`vec` をソートしたもの） |
| `g.distinct(n, lo, hi)` | `[lo, hi]` から相異なる n 個、順番はランダム |
| `g.perm(n)` | 順列。1 始まりなら 1..n、`one_indexed = false` なら 0..n-1 |
| `g.partition(sum, k, min_value = 0)` | 和が `sum` になる k 個の整数（各要素は `min_value` 以上） |

| `sorted` のオプション | 既定 | 意味 |
| --- | --- | --- |
| `.strict` | `false` | `true` なら狭義（`distinct` をソートしたもの） |
| `.desc` | `false` | `true` なら減少。`.strict` と組み合わせられる |

- `vec` `sorted`: `i` を使うので、`lo` や `hi` が重複して出やすい。
- `sorted`: `.strict` のときは `distinct` を使うので端の偏りはなく、範囲の中の値が n 個より少ないとエラー。
- `distinct`: `uniform` を使うので端の偏りはない。範囲の中の値が n 個より少ないとエラー。
- `partition`: `sum - min_value * k` を k 個に分ける区切り位置を一様に引いて作る。0 を含みたくないときは `min_value = 1`。`sum < min_value * k` だとエラー。

### seq の形

`vec` は各要素を独立に引くだけなので、全部同じ値やソート済みのような形はほぼ出ない。`seq` は形を選んでから作る。`.shape` を省略すると毎回混ぜ、選んだ形を `seq=形` のタグにする。

```cpp
auto a = g.seq(n, 1, 1000000000);                          // 形は混ぜる
auto a = g.seq(n, 1, 100, {.shape = SeqShape::few});
auto a = g.seq(n, Range{-5, 5}, {.shape = SeqShape::edge});
```

| 形 | 中身 |
| --- | --- |
| `random` | `vec(n, lo, hi)` と同じ（mixed では 2 倍の重み） |
| `same` | 全部同じ値 |
| `few` | `i(lo, hi)` で引いた 2〜3 種類の値だけ |
| `asc` | 広義増加（`sorted(n, lo, hi)`） |
| `desc` | 広義減少（`sorted(n, lo, hi, {.desc = true})`） |
| `edge` | `lo` と `hi` だけ |

`vec` の動きは変わらない。座標や重みのように一様でいい部品は `vec`、配列そのものが入力の主役なら `seq` を使うといい。狭義の単調列は `seq` には無いので `sorted` の `.strict` を使う。

## 木

```cpp
auto es = g.tree(n);                                        // 形は混ぜる
auto es = g.tree(n, {.shape = TreeShape::path});
auto es = g.tree(n, {.shape = TreeShape::star, .w = Range{1, 100}});
g.out(es);                                                  // 1 本 1 行で n-1 行
```

| 関数 | 返すもの |
| --- | --- |
| `g.tree(n, {...})` | n 頂点の木の辺のリスト（n-1 本） |

| オプション | 既定 | 意味 |
| --- | --- | --- |
| `.shape` | `TreeShape::mixed` | 木の形（下の表） |
| `.w` | なし | 重み（Range かラムダ） |

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

## 根付き木（親の列）

```cpp
auto p = g.parents(n);                                      // P_2 .. P_n（n-1 個）、P_i < i
g.out(p);                                                   // "1 1 2 3 ..." の 1 行
auto p = g.parents(n, {.shape = TreeShape::path});
auto p = g.parents(n, {.ordered = false});                  // n 個、根の親は 0
```

| 関数 | 返すもの |
| --- | --- |
| `g.parents(n, {...})` | 根付き木の親の列。既定は P_2 .. P_n の n-1 個で、P_i < i |

「頂点 i の親は P_i」の形で入力される根付き木を作る。`.shape` は `tree` と同じで、その形の木を作ってから根を決める。根は半々の確率で木を作ったときの最初の頂点（path や deep なら端）、それ以外はランダムな頂点にする。

| オプション | 既定 | 意味 |
| --- | --- | --- |
| `.shape` | `TreeShape::mixed` | 木の形（[木](#木) の表と同じ） |
| `.ordered` | `true` | `true` なら根を 1 にして、親が子より先に来るように番号を振る。返すのは P_2 .. P_n の n-1 個で、常に P_i < i |

`.ordered = true` の番号は、根から始めて「親に番号が付いている頂点」の中からランダムに 1 つずつ選んで振る。なので path の形で根が端なら `1 2 3 ...` になるが、一般には BFS 順でも DFS 順でもない。

`.ordered = false` なら番号をシャッフルし、根もどの番号になるか分からない。返すのは P_1 .. P_n の n 個で、根のところは `base() - 1`（1 始まりなら 0、0 始まりなら -1）。問題で根の親が -1 などと決まっているなら、その値に書き換えて出す。

重み付き（「P_i W_i」など）の専用の形はない。ループで出す。

```cpp
auto p = g.parents(n);
for (int i = 0; i < n - 1; ++i) g.out(p[i], g.i(1, 100));   // "P_i W_i" を n-1 行
```

## グラフ

```cpp
auto es = g.graph(n, m);                                    // 単純な無向グラフ。形は混ぜる
auto es = g.graph(n, m, {.connected = true, .w = Range{1, 1000000000}});
auto es = g.graph(n, m, {.simple = false});                 // 自己ループと多重辺あり
auto es = g.graph(n, m, {.shape = GraphShape::hub});        // 形を指定
```

| 関数 | 返すもの |
| --- | --- |
| `g.graph(n, m, {...})` | n 頂点 m 辺の無向グラフの辺のリスト |

有向グラフは [有向グラフ](#有向グラフ) の `digraph` を使う。

| オプション | 既定 | 意味 |
| --- | --- | --- |
| `.shape` | `GraphShape::mixed` | 辺の張り方（下の表） |
| `.connected` | `false` | 連結にする。先に全域木を入れてから残りの辺を足す。`m >= n - 1` が必要 |
| `.simple` | `true` | 自己ループと多重辺を作らない。`false` だとどちらも出る |
| `.w` | なし | 重み（Range かラムダ） |

| `.shape` | 形 |
| --- | --- |
| `GraphShape::random` | 一様ランダムに辺を張る。`connected` なら土台の全域木は [木](#木) の mixed と同じように形を混ぜる（path や deep なら直径の長いグラフになる） |
| `GraphShape::bipartite` | `bipartite(n, m, ...)` と同じ（左右の大きさはランダム） |
| `GraphShape::pieces` | 頂点を 2 つ以上の組に分け、組の中だけに辺を張る。連結成分が必ず 2 つ以上になり、m が小さければ森になる。`connected` とは一緒に使えず、n >= 2 が必要 |
| `GraphShape::hub` | 1 から 3 個の頂点を中心に選び、辺の 8 割は中心から張る（次数が偏る）。`connected` なら土台はスター |
| `GraphShape::path` | 頂点を一列に並べ、番号の差が w 以下の頂点どうしだけに辺を張る（直径が長い）。w は 1 から 3 で、m 本入らないときだけ広げる。`connected` なら土台はパス |
| `GraphShape::mixed`（省略時） | 上のどれかを選ぶ（下の説明） |

mixed は random を 2 つ、hub と path を 1 つずつ入れた候補に、条件に合えば次を足して、その中から 1 つ選ぶ。

- bipartite（単純グラフで m 本入らないときは足さない）
- `connected` でなく n >= 2 なら pieces（m 本入らないときは足さない）

なので連結でなければ random が 2/6、他が 1/6 ずつになる。木は混ぜない。木も試したいときは、`m = n - 1` で `g.tree(n)` を出す生成器を STRESS_GEN で別に作る。そうすれば失敗したときに、どの生成器だったかで木かどうかも分かる。

形を指定したときに条件に合わなければエラーになる（`connected` で pieces、m が多すぎるなど）。単純グラフで `m` が n(n-1)/2 を超えるとエラー。辺の順番と頂点番号は木と同じくシャッフルされ、各辺の `u v` の向きもランダム。

## 有向グラフ

```cpp
auto es = g.digraph(n, m);                                  // 単純な有向グラフ。形は混ぜる
auto es = g.digraph(n, m, {.reach = Reach::root});          // 頂点 1 から全頂点に到達できる
auto es = g.digraph(n, m, {.reach = Reach::strong});        // 強連結
auto es = g.digraph(n, m, {.shape = DigraphShape::dag, .reach = Reach::root});
```

| 関数 | 返すもの |
| --- | --- |
| `g.digraph(n, m, {...})` | n 頂点 m 辺の有向グラフの辺のリスト。各辺 `u v` は u から v への辺 |

| オプション | 既定 | 意味 |
| --- | --- | --- |
| `.shape` | `DigraphShape::mixed` | 辺の張り方（下の表） |
| `.reach` | `Reach::none` | 到達性（下の表） |
| `.simple` | `true` | 自己ループと多重辺を作らない（u→v と v→u は別の辺なので両方出ることがある） |
| `.w` | なし | 重み（Range かラムダ） |

| `.reach` | 保証すること | 作り方 | 必要な m |
| --- | --- | --- | --- |
| `Reach::none` | なし | | |
| `Reach::weak` | 弱連結（向きを無視すれば連結） | 全域木を入れ、各辺の向きをランダムにする | `m >= n - 1` |
| `Reach::root` | 頂点 1 から全頂点に到達できる | 全域木を入れ、根を頂点 1 にして辺を全部根から外向きにする | `m >= n - 1` |
| `Reach::strong` | 強連結（どの頂点からどの頂点へも到達できる） | 耳分解で作る（下の説明） | n >= 2 なら `m >= n` |

どれも、土台を入れたあとの残りの辺は形に従って足す。`Reach::root` の根は `g.one_indexed = false` なら頂点 0 になる。根以外の番号はシャッフルされる。

`Reach::strong` の土台は、頂点 1 個から始めて「今ある頂点 → 新しい頂点の列 → 今ある頂点」の経路（耳）を何本か足して作る。耳の本数は 1 から min(n-1, m-(n-1)) の中からランダムに選ぶので、1 本なら大きな閉路 1 つ、多ければ小さな閉路がいくつもつながった形になる。土台の辺は (n-1) + 耳の本数 本。

| `.shape` | 形 |
| --- | --- |
| `DigraphShape::random` | 一様ランダムに辺を張る。`weak` と `root` では土台の全域木の形を [木](#木) の mixed と同じように混ぜる |
| `DigraphShape::dag` | `dag(n, m, ...)` と同じ。`Reach::strong` とは一緒に使えない |
| `DigraphShape::pieces` | 頂点を 2 つ以上の組に分け、組の中だけに辺を張る。`.reach` は `none` だけで、n >= 2 が必要 |
| `DigraphShape::hub` | 1 から 3 個の頂点を中心に選び、辺の 8 割は中心とのあいだに張る（向きはランダム）。`weak` と `root` では土台はスター |
| `DigraphShape::path` | 頂点を一列に並べ、番号の差が w 以下の頂点どうしだけに辺を張る（向きはランダム）。`weak` と `root` では土台はパス（`root` なら根は半々で端） |
| `DigraphShape::mixed`（省略時） | 上のどれかを選ぶ（下の説明） |

`Reach::strong` の土台は形によらず耳分解なので、hub と path は残りの辺だけがその形になる。

mixed は random を 2 つ、hub と path を 1 つずつ入れた候補に、条件に合えば次を足して、その中から 1 つ選ぶ。

- `Reach::strong` でなければ dag（単純グラフで m 本入らないときは足さない）
- `Reach::none` で n >= 2 なら pieces（m 本入らないときは足さない）

形を指定したときに条件に合わなければエラーになる。単純グラフで `m` が n(n-1) を超えるとエラー。

## DAG

```cpp
auto es = g.dag(n, m);                                      // 単純な DAG
auto es = g.dag(n, m, {.reach = Reach::weak});              // 弱連結
auto es = g.dag(n, m, {.reach = Reach::root});              // 頂点 1 から全頂点に到達できる
auto es = g.dag(n, m, {.increasing = true});                // 常に u < v
```

| 関数 | 返すもの |
| --- | --- |
| `g.dag(n, m, {...})` | n 頂点 m 辺の DAG の辺のリスト |

ランダムなトポ順を作り、トポ順で前の頂点から後ろの頂点への辺だけを張る。頂点番号と辺の順番はシャッフルするので、出力からトポ順は分からない。

| オプション | 既定 | 意味 |
| --- | --- | --- |
| `.reach` | `Reach::none` | `Reach::weak` なら弱連結。`Reach::root` なら頂点 1 をトポ順の先頭にして、各頂点に前の頂点からの辺を 1 本ずつ入れる（頂点 1 から全頂点に到達できる）。どちらも `m >= n - 1` が必要。DAG は強連結にならないので `Reach::strong` はエラー |
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

| 関数 | 返すもの |
| --- | --- |
| `g.bipartite(n1, n2, m, {...})` | 左 n1 頂点、右 n2 頂点、m 辺の二部グラフの辺のリスト |
| `g.bipartite(n, m, {...})` | 総頂点数 n の二部グラフ。左右の大きさは m 本の辺が入る分け方からランダムに選ぶ |

- `bipartite(n1, n2, m)`: 既定で左右に別々の番号を振る。各辺 `u v` の `u` が左（1 から n1）、`v` が右（1 から n2）で、マッチングの問題によくある入力形式になる。`.merged = true` にすると、n1+n2 頂点に通し番号をシャッフルして振り、各辺の向きもランダムにする。「二部グラフか判定せよ」のような、普通のグラフとして入力される問題向け。
- `bipartite(n, m)`: 番号は常に通し番号（`.merged` は無視される）。

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

`g.one_indexed` が `true`（既定）なら `tree` / `parents` / `graph` / `digraph` / `dag` / `bipartite` / `perm` は 1 始まり、`false` なら 0 始まりになる。生成器の最初で `g.one_indexed = false;` と書けばその生成器の中で全部 0 始まりになる。

## 文字列

```cpp
auto s = g.str(n, "ab");                                    // 形は混ぜる
auto s = g.str(n, "abc", {.shape = StrShape::palindrome});
auto s = g.str(n, [&] { return g.coin(0.1) ? 'b' : 'a'; });
```

| 関数 | 返すもの |
| --- | --- |
| `g.str(n, alphabet, {...})` | alphabet の文字だけを使った長さ n の文字列 |
| `g.str(n, f)` | 各文字を `f()` で引いた長さ n の文字列 |
| `g.mutate(s, k, alphabet)` | s のランダムな位置を k 回書き換えた文字列。alphabet を省略すると s に出てくる文字から選ぶ |
| `g.strs(k, total, alphabet, {...})` | k 個の文字列。長さの総和は `i(k, total)` |
| `g.grid(h, w, ".#")` `g.grid(h, w, f)` | h 行 w 列のグリッド（`vector<string>`）。各マスを一様に選ぶか `f()` で引く |

`str` と `strs` のオプションは `.shape` だけ。

| `.shape` | 形 |
| --- | --- |
| `StrShape::random` | 各文字を一様に選ぶ |
| `StrShape::periodic` | 周期 p の繰り返し（p は半々の確率で 1 から min(n, 4)、または 1 から n/2）に、0 から 2 文字の書き換えを加える |
| `StrShape::palindrome` | 回文。半々の確率で全体が 1 つの回文、または 2 から 4 個の回文をつなげたもの |
| `StrShape::fibonacci` | 2 種類の文字で作るフィボナッチ文字列の一部分 |
| `StrShape::runs` | 同じ文字の連続（ラン）を並べたもの。隣のランは違う文字 |
| `StrShape::mixed`（省略時） | 上のどれかを選ぶ（random が 2/6、他は 1/6 ずつ） |

- `str`: periodic は書き換えが入るので、ちょうど周期的とは限らない（周期的に近いケースを作るため）。alphabet が 1 文字なら、どの形でも同じ文字の繰り返しになる。
- `mutate`: 同じ位置や同じ文字を選ぶこともあるので、実際に変わる文字数は k 以下。
- `strs`: 各長さが 1 以上になるように `partition` で分ける。`total < k` だとエラー。

## 巨大整数

```cpp
auto s = g.big(100);                                        // ちょうど 100 桁の 10 進数
auto s = g.big(5, {.lead_zero = true});                     // "00731" なども出る
auto s = g.big(8, {.digits = digits::HEX});                 // 16 進、大文字
auto s = g.big_range("1", "1e100");                         // 1 以上 10^100 以下
auto s = g.big_range("0", "2^60-1", {.digits = digits::bin});   // 60 桁以下の 2 進
auto s = g.big_range("1", "1e18", {.digits = digits::hex});      // 範囲は 10 進、出力は 16 進
```

| 関数 | 返すもの |
| --- | --- |
| `g.big(len, {...})` | ちょうど len 桁の数（`std::string`） |
| `g.big_range(lo, hi, {...})` | 閉区間 `[lo, hi]` の数（`std::string`）。`lo` `hi` の書き方は [範囲の書き方](#範囲の書き方) |

| オプション | 既定 | 意味 |
| --- | --- | --- |
| `.digits` | `digits::dec` | 出力の進数（下の `digits::` の表）。`big` と `big_range` の両方 |
| `.lead_zero` | `false` | `true` なら先頭の桁が 0 でもいい。`big` だけ |

どちらも `g.out(s)` でそのまま出せる。

- `big`: 各桁は一様で、`.lead_zero = false` なら先頭の桁は 0 にしない。ただし len = 1 なら `"0"` も出る。
- `big_range`: 値で一様に選ぶとほとんどが最大桁数になってしまうので、先に桁数を一様に選び、その桁数の中で上の桁から順に選ぶ（値としては厳密な一様ではない）。1/8 の確率で端の値を返す。端の値は `lo`、`lo + 1`、`hi - 1`、`hi` と、範囲に入る b^k と b^k - 1（10 進なら `1000…0` と `999…9`）から選ぶ。
- `big_range`: 結果に先頭の 0 は付かない。`lo > hi` だとエラー。
- 負の数は扱わない。必要なら `(g.coin() ? "-" : "") + s` のように自分で付ける（`"-0"` に注意）。

### 範囲の書き方

`.digits` は出力の進数だけを決める。`lo` と `hi` はどの進数でも下の形で書く。問題文の制約はふつう 10 進や累乗で書かれているので、それをそのまま写せばいい。

| 書き方 | 意味 | 例 |
| --- | --- | --- |
| `123` | 10 進の整数（先頭の 0 はあってもいい） | `"0"` `"1000000007"` |
| `AeK` | A × 10^K（A は 10 進の整数） | `"1e18"` `"2e5"` |
| `B^K` | B^K（B は 2 以上、B と K は 10 進の整数） | `"2^60"` `"16^200000"` |
| 上のどれか + `-1` | 1 を引いた数 | `"2^60-1"` `"1e18-1"` |

読めるのはこの形だけで、式のパーサではない（`2^60+5` や `(10^9+7)*2`、階乗などは書けない）。それ以外の値は 10 進の数字で直接書く。形が違うとエラーになる。

### 計算量

範囲の文字列は呼ぶたびに出力の進数に変換する。計算量は次の通り（D は範囲を 10 進で書いたときの桁数、L は出力の進数での桁数）。

| 場合 | 計算量 | 目安（-O2） |
| --- | --- | --- |
| 出力が 10 進（`123` / `AeK`） | O(D) | 一瞬 |
| `B^K` で B が出力の進数と同じ（hex 出力で `16^K` など） | O(K) | `16^200000-1` で 1 回 3 ms ほど |
| それ以外（10 進の数を b 進にする、B が違う `B^K`） | O(D × L / 18)、つまり桁数の 2 乗 | 1 万桁で 15 ms、10 万桁で 1.5 秒 |

なので数万桁を超える範囲を 10 進以外で出したいときは、出力と同じ底の `B^K` で書く。乱数で 1 個作る部分はどの場合も O(L)。

`.digits` は使う数字を値の小さい順に並べた文字列で、長さが進数になる。用意してある定数:

| 定数 | 中身 |
| --- | --- |
| `digits::bin` | `01` |
| `digits::oct` | `01234567` |
| `digits::dec`（既定） | `0123456789` |
| `digits::hex` | `0123456789abcdef` |
| `digits::HEX` | `0123456789ABCDEF` |
| `digits::base36` | `0123456789abcdefghijklmnopqrstuvwxyz` |

ほかの並びも文字列で直接渡せる（`{.digits = "0123456789ABCDEFGHIJ"}` で 20 進など）。2 文字以上必要。

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

## タグ

ケースの種類を記録しておくと、失敗したときに `st` がそれを表示する。

```text
WA  token 1: main='5' ref='6'
case=9473625110285193:3:random  ->  st repro 9473625110285193:3:random
tags: graph=hub, str=palindrome x3, heavy
```

mixed の形で選ばれたものは自動で付く。形を指定したときは付かない。

| タグ | 付くとき |
| --- | --- |
| `tree=形` | `tree(n)` で mixed が選んだ形 |
| `parents=形` | `parents(n)` で mixed が選んだ形 |
| `graph=形` `digraph=形` | `graph` / `digraph` で mixed が選んだ形 |
| `graph.base=形` `digraph.base=形` | random の形で土台の木を入れたとき（`graph` の `connected`、`digraph` の `weak` / `root`）、その木の形 |
| `str=形` | `str` / `strs` で mixed が選んだ形 |
| `seq=形` | `seq` で mixed が選んだ形 |

| 関数 | 動き |
| --- | --- |
| `g.tag(name)` | ケースにタグを付ける。生成器の中で分岐したときの目印などに使う |

```cpp
STRESS_GEN(random) {
    int n = g.size(1, 10), q = g.size(1, 10);
    bool heavy = g.coin(0.2);
    if (heavy) g.tag("heavy");                              // 同じクエリばかりのケース
    g.tag("q=" + std::to_string(q));                        // 値をタグにしてもいい
    ...
}
```

- 同じタグを何度付けても 1 つにまとめ、2 回以上なら `x3` のように回数を出す。`strs(k, ...)` で palindrome が 3 本選ばれれば `str=palindrome x3` になる。
- 並びは最初に付けた順。
- タグは入力には出ない。`./gen` を手で実行したときも何も出さない（`st` が環境変数 `STRESS_TAGS` に書き出し先を渡したときだけ、そのファイルに書く）。
- 保存した失敗の `meta.json` の `tags` にも入る。
- 生成器の名前（STRESS_GEN の名前）は `case=...` の最後に出るので、タグにしなくていい。

## 前の答えで復号するクエリ

「クエリは前のクエリの答え last を使って l = l' xor last のように復号する」形式の問題は、gen.cpp の中で愚直に答えを計算しながら、暗号化したクエリを出せばいい。ドライバの設定は普通の問題と同じで、main も愚直解も暗号化された入力を読む。

```cpp
// 区間和。l = l' xor last, r = r' xor last（last は前の答え、最初は 0）
STRESS_GEN(online) {
    int n = g.size(1, 8), q = g.size(1, 8);
    auto a = g.vec(n, 1, 10);
    g.out(n, q);
    g.out(a);
    long long last = 0;
    for (int k = 0; k < q; ++k) {
        long long l = g.i(1, n), r = g.i(1, n);
        if (l > r) std::swap(l, r);
        g.out(l ^ last, r ^ last);                          // 暗号化して出す
        last = 0;                                           // 愚直に答えを求める
        for (long long i = l; i <= r; ++i) last += a[i - 1];
    }
}
```

- gen の中の答えが正しくないと、正しい解でも復号した値が範囲外になる。gen の答えの計算は愚直解と同じくらい単純に書く。
- main が途中で答えを間違えると、そこから先は main の復号がずれる。出力は食い違うので、失敗としてちゃんと見つかる。最初に食い違った行がバグのあるクエリになる。
- 答えを mod で取る、答えが無いときは -1 を使う、など last の決め方は問題に合わせて書く。
