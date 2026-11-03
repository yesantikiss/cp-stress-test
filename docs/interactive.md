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
| `it.ensure(cond, msg)` / `it.wrong(msg)` | WA |
| `it.accept()` / `it.fail(msg)` | AC で終わる / interactor 自身の失敗 |

`interact` から戻ると、main が出力を閉じるまで待ち、余分な出力があれば WA にする。終了コードは checker と同じで 0 = AC, 1 = WA, 3 = interactor の失敗。

## 適応的な interactor

答えを最初に決めず、解答の質問に合わせて後から決める interactor。「運よく当たる」解を落とせる。数当てなら、x を持たずに「x としてまだありうる範囲 `[lo, hi]`」だけを持ち、範囲が広く残る方を答える。

```cpp
#include "stress/interact.hpp"

void interact(Interactor& it) {
    int n;
    unsigned long long seed;
    it.in >> n >> seed;  // gen は g.out(n, g.i(0, 1000000000)) のように seed も出す
    it.send(n);
    it.query_limit(25);
    int lo = 1, hi = n;  // x としてありうる範囲
    while (true) {
        std::string t;
        it.out >> t;
        if (t == "?") {
            int y;
            it.out >> y;
            it.ensure(1 <= y && y <= n, "y is out of range");
            it.count_query();
            // Yes（x <= y）なら [lo, min(hi, y)]、No なら [max(lo, y + 1), hi] が残る
            long long yes = std::max(0, std::min(hi, y) - lo + 1);
            long long no = std::max(0, hi - std::max(lo, y + 1) + 1);
            // 広く残る方を答える。同じ広さなら seed で決める
            bool say_yes = yes != no ? yes > no : (seed >> (it.queries() % 64)) & 1;
            if (say_yes) hi = std::min(hi, y);
            else lo = std::max(lo, y + 1);
            it.send(say_yes ? "Yes" : "No");
        } else if (t == "!") {
            int ans;
            it.out >> ans;
            // 1 つに絞れていて、それと一致したときだけ正解
            it.ensure(lo == hi && ans == lo, "answer is not determined yet");
            return;
        } else {
            it.wrong("unknown command: " + t);
        }
    }
}
```

幅 2 まで絞ったところで当てずっぽうに答える解は、x を固定した interactor だとたまたま当たって通ることがある。この interactor なら、幅 2 で答えた時点で必ず WA になる。

書くときの注意:

- 判定は「これまでの答えと矛盾しない x が 1 つしかなく、それを答えた」にする。x を途中で 1 つに決めてしまうと、適応的にした意味がなくなる。
- 答え方を乱数で決めるときは、seed を gen の出力に入れて `it.in` から読む。interactor の中で時刻などから seed を作ると `st repro` で同じやり取りにならない。
- 最悪の答え方だけだと、解の偏り（いつも左を先に聞く、など）に気づけないことがある。gen に「適応的」と「固定」の 2 種類の生成器を作り、入力の先頭の値で interactor の振る舞いを切り替えるのもよい。

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
