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
- 適応的（adaptive）な interactor も書ける。乱数を使うなら seed を入力に含めておくと再現できる。

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
