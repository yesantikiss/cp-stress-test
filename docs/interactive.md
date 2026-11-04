# インタラクティブ問題

interactor.cpp を書いて `st -i interactor.cpp` で回す。main.cpp とは st が間に入ってつなぎ、やり取りを全部 `transcript.txt` に残す。

```bash
st init -i                 # gen.cpp と interactor.cpp の雛形を置く
st -i interactor.cpp       # main.cpp と interactor をつないで 1000 回
st -i interactor.cpp -t 60 --tl 1 --san
st repro SEED:CAP:NAME -i interactor.cpp
st last                    # transcript も表示する
```

`-i` は `-c`、`--ref`、`--cmp` とは一緒に使えない。interactor.cpp があっても `-i` を付けなければ普通の比較モードのまま。

## gen.cpp と愚直解

- gen.cpp は今までどおり書く。出力は interactor が読むので、隠し情報（数当てなら答えの数）も含める。main は in.txt を見ない。
- 愚直解とは比べない。正しいかどうかは interactor が隠し情報から判定する。guchoku.cpp はなくてよい。
- 名前付き生成器、cap を小さい方から増やすこと、失敗後の縮小、`st repro` はそのまま動く。
- 適応的（adaptive）な interactor も書ける（[下の例](#適応的な-interactor)）。乱数を使うなら seed を入力に含めておくと再現できる。

## interactor.cpp

例: 1 以上 n 以下の隠した数 x を 25 回以内の質問で当てる。

```cpp
#include "stress/interact.hpp"

void interact(Interactor& it) {
    int n, x;
    it.in >> n >> x;            // gen の出力
    it.send(n);                 // main へ 1 行送る（改行と flush は自動）
    it.query_limit(25);
    while (true) {
        std::string t;
        it.out >> t;            // main の出力を読む
        if (t == "?") {
            int y;
            it.out >> y;
            it.ensure(1 <= y && y <= n, "y is out of range");
            it.count_query();   // 26 回目で WA
            it.send(x <= y ? "Yes" : "No");
        } else if (t == "!") {
            int ans;
            it.out >> ans;
            it.ensure(ans == x, "wrong answer");
            return;             // 戻ったら AC（main が余計な出力をせずに終わるかも確かめる）
        } else {
            it.wrong("unknown command: " + t);
        }
    }
}
```

| 書き方 | 意味 |
| --- | --- |
| `it.in` | gen の出力（in.txt）。読めないときは interactor の失敗 |
| `it.out` | main の出力。届いた分から順に読む。読めない、main が先に終わった、のときは WA |
| `>>` | `int` / `long long` / `double` / `char` / `std::string` を読む。型の範囲外も WA。値の範囲は自分で `ensure` する |
| `read_int(lo, hi)` など | checker と同じ関数も使える（範囲外は自動で WA） |
| `it.send(a, b, ...)` | 空白区切りで 1 行送って flush。`vector` は要素を並べる |
| `it.query_limit(q)` / `it.count_query()` | 回数制限。超えたら WA「query limit exceeded (26 > 25)」 |
| `it.queries()` | これまでに `count_query` した回数 |
| `it.ensure(cond, msg)` / `it.wrong(msg)` | WA |
| `it.accept()` / `it.fail(msg)` | AC で終わる / interactor 自身の失敗 |

`>>` や `read_int` などの読み取りは checker と同じ `Reader` で、細かい振る舞いは [checker.cpp の書き方](checker.md#読み取り) にある。`STRESS_NO_MAIN` も checker と同じく使える。

`interact` から戻ると、main が出力を閉じるまで待ち、余分な出力があれば WA にする。終了コードは checker と同じで 0 = AC, 1 = WA, 3 = interactor の失敗。

## 適応的な interactor

答えを最初に決めず、解答の質問に合わせて後から決める interactor。「運よく当たる」解を落とせる。

`stress/adaptive.hpp` を使うと、隠し情報としてありうるもの（候補）を全部渡して、「その候補が答えだったら何と返すか」を書くだけで済む。質問のたびに、候補が一番多く残る返事を選んで、それと矛盾する候補を捨てる。

```cpp
#include "stress/interact.hpp"
#include "stress/adaptive.hpp"

void interact(Interactor& it) {
    int n;
    unsigned long long seed;
    it.in >> n >> seed;  // gen は g.out(n, g.i(0, 1000000000)) のように seed も出す
    it.send(n);
    it.query_limit(25);
    Adaptive ad(cand::range(1, n), seed);  // x としてありうる値を全部候補にする
    while (true) {
        std::string t;
        it.out >> t;
        if (t == "?") {
            int y;
            it.out >> y;
            it.ensure(1 <= y && y <= n, "y is out of range");
            it.count_query();
            bool yes = ad.reply([&](long long x) { return x <= y; });  // x が答えなら何と返すか
            it.send(yes ? "Yes" : "No");
        } else if (t == "!") {
            int ans;
            it.out >> ans;
            // 残っている候補すべてで正しい = 1 つに絞れていて、それを答えた
            ad.ensure_all([&](long long x) { return x == ans; }, "answer is not determined yet");
            return;
        } else {
            it.wrong("unknown command: " + t);
        }
    }
}
```

幅 2 まで絞ったところで当てずっぽうに答える解は、x を固定した interactor だとたまたま当たって通ることがある。この interactor なら、幅 2 で答えた時点で必ず WA になり、「hidden could be 7, 2 candidates left」のように反例の候補が出る。

隠し情報は何でもよい。1..n の順列 p で「`? a b` に p[a] < p[b] か答える」なら次のようになる。

```cpp
Adaptive ad(cand::permutations(n), seed);
bool r = ad.reply([&](const std::vector<int>& p) { return p[a - 1] < p[b - 1]; });
```

| 書き方 | 意味 |
| --- | --- |
| `Adaptive ad(候補, seed)` | 候補は `std::vector`。同数の返事が複数あるときは seed で選ぶ |
| `Adaptive ad(候補, seed, Strategy::fixed)` | 最初に候補を 1 つ seed で選び、それに従って返す（普通のジャッジと同じ答え方） |
| `ad.reply(f)` | `f(候補)` が返事。一番多く残る返事を返し、矛盾する候補を捨てる。返事は `==` と `<` で比べられる型（`bool`、`int`、`std::string`、`pair`、`vector` など） |
| `ad.ensure_all(pred, msg)` | 残りの候補すべてで `pred` が成り立たなければ WA。反例の候補も出す |
| `ad.size()` / `ad.determined()` | 残りの候補の数 / 1 つに決まったか |
| `ad.only()` | 1 つに決まったときのその候補（決まっていなければ interactor の失敗） |
| `ad.candidates()` | 残りの候補 |

### 例

`examples/adaptive/` に、gen.cpp、interactor.cpp、正しい main.cpp をそろえた例がある。フォルダに入って `st -i interactor.cpp` でそのまま動く。

| 例 | 隠し情報 | 使うもの |
| --- | --- | --- |
| [guess](../examples/adaptive/guess) | 1..n の整数（数当て） | `cand::range`、返事は `bool` |
| [sort](../examples/adaptive/sort) | 1..n の順列（大小比較） | `cand::permutations` |
| [tree](../examples/adaptive/tree) | n 頂点の木（距離を聞く） | `cand::trees`、返事は `int` |
| [arc184_a](../examples/adaptive/arc184_a) | [ARC184 A](https://atcoder.jp/contests/arc184/tasks/arc184_a) の偽コイン 10 枚 | `AdaptiveState`（候補を並べられない例） |

### 候補の作り方

`cand::` の関数で全列挙できる。条件付きの候補は、作ってから `std::erase_if` で絞る。

| 関数 | 候補 | 現実的な大きさ |
| --- | --- | --- |
| `cand::range(l, r)` | l..r の整数（`long long`） | 10^6 くらいまで |
| `cand::tuples(k, l, r)` | 各要素が l..r の長さ k の列（重複あり）。配列や 0/1 列 | (r-l+1)^k が 10^6 くらいまで |
| `cand::strings(k, "abc")` | 指定した文字を使う長さ k の文字列 | 同上 |
| `cand::permutations(n)` | 1..n の順列 | n ≤ 9 くらい |
| `cand::combinations(n, k)` | 1..n から k 個選んだ昇順の列 | C(n, k) が 10^6 くらいまで |
| `cand::trees(n)` | 頂点 1..n のラベル付き木。辺 (u, v)（u < v）を昇順に並べた列 | n ≤ 8 くらい（n^(n-2) 個） |

列はどれも 0 始まりで、値は 1 始まり。問題の i 番目（1 始まり）は `p[i - 1]` で読む。

```cpp
auto cs = cand::tuples(5, 1, 3);
std::erase_if(cs, [](const auto& a) { return a[0] == a[4]; });  // 両端が違う列だけ
```

1 回の `reply` は候補の数に比例する時間がかかる。候補が 10^7 を超えると interactor の失敗にする。

### 候補を並べられないとき

候補が多すぎるときは、状態を自分で持って `AdaptiveState` を使う。

| 書き方 | 意味 |
| --- | --- |
| `AdaptiveState st(初期状態, seed)` | 状態は好きな型でいい |
| `st.reply({返事...}, apply, score)` | 返事ごとに状態のコピーへ `apply(s, 返事)` し、`score(s)` が一番大きい返事を返す（同点は seed で選ぶ）。`apply` が `false` を返した返事は矛盾するので選ばない |
| `st.state()` | 今の状態 |

```cpp
AdaptiveState st(std::pair<int, int>{1, n}, seed);  // x としてありうる範囲 [lo, hi]
bool yes = st.reply({true, false},
    [&](auto& s, bool r) {
        if (r) s.second = std::min(s.second, y); else s.first = std::max(s.first, y + 1);
        return s.first <= s.second;
    },
    [](const auto& s) { return s.second - s.first + 1; });  // 残りの数
```

最後の判定は自分で書く（上の例なら `it.ensure(st.state().first == st.state().second && ...)`）。

`score` は残りの候補の正確な数でなくてもよい。数えるのが重い問題では、目安の点数（決まっていない組の数など）にする。

### 限界

- 「一番多く残る返事」は 1 手先しか見ないので、本当の最悪ケースではない。二分探索のような問題ではこれが最悪だが、問題によっては、この interactor には勝てても本番の適応的ジャッジで落ちる解がありうる。
- gen の乱択で候補を K 個作って渡すこともできるが、だいたい log2(K) 回の質問で候補が 1 つになり、そこから先は固定のジャッジと同じになる。候補にない答えは考えないので、まだ決まっていないのに答えた解を通してしまうこともある。質問回数が少ない問題で、ないよりはまし、くらいに使う。
- 最悪の答え方だけだと、解の偏り（いつも左を先に聞く、など）に気づけないことがある。gen に「適応的」と「固定」の 2 種類の生成器を作り、入力の値で `Strategy::fixed` に切り替えるのもよい。

### 自分で書くときの注意

- 判定は「これまでの返事と矛盾しない答えが 1 つしかなく、それを答えた」にする。途中で答えを 1 つに決めてしまうと、適応的にした意味がなくなる。
- 答え方を乱数で決めるときは、seed を gen の出力に入れて `it.in` から読む。interactor の中で時刻などから seed を作ると `st repro` で同じやり取りにならない。

## 判定

上から順に見て、最初に当てはまったものにする。

| 状況 | 判定 |
| --- | --- |
| main が TL を超えた | TLE |
| main が interactor より先に異常終了した | RE |
| interactor が終了コード 3、落ちた、または TL を超えた | INTERACTOR_FAIL（そこで止める） |
| interactor が WA を出した | WA（main はその時点で止める） |
| interactor が AC のあと main が異常終了した | RE |
| どちらも正常終了 | OK |

- TL は main が起動してから終わるまでの実時間。interactor の待ち時間も含むので、本番より少し厳しい。
- flush 忘れは互いに待ち合って TLE になる。最後に interactor が送ったあと main から何も来ていなければ「flush 忘れ？」と添える。`ios::sync_with_stdio(false); cin.tie(nullptr);` を入れたまま `'\n'` で出すと起きる。
- `count_query` を使っていれば、OK のときも「max queries 23/25」のように一番多かった回数を出す。

## ログ

失敗は `.stress/fail/<日時>-<種類>-.../` に in.txt、transcript.txt、stderr_main.txt、stderr_interactor.txt、meta.json として残る。画面には transcript の最初と最後の 20 行を出す。

```text
< 10          interactor -> main
> ? 5         main -> interactor
< No
> ! 7
# [interactor] WA: wrong answer
```

1 行が 2000 文字を超えると切り、行数が 1 万を超えると先頭と末尾 5000 行ずつだけ残す。
