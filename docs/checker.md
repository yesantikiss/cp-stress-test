# checker.cpp

答えが複数ある問題では、愚直解と出力を比べる代わりに checker で判定する。`stress/judge.hpp` を include して `judge` を書き、`st -c checker.cpp` で回す。

```cpp
#include "stress/judge.hpp"

void judge(Judge& j) {
    long long n = j.in.read_int();
    long long x = j.out.read_int(1, n);   // 形式違反・範囲外は自動で WA
    j.ensure(x % 2 == 0, "x must be even");
    j.out.expect_eof();                   // 余分な出力があれば WA
}
```

```bash
st init -c              # gen.cpp と checker.cpp の雛形を置く
st -c checker.cpp       # main.cpp の出力を checker で判定する（guchoku.cpp は使わない）
st -c checker.cpp --ref # 愚直解も走らせ、その出力を j.ref で読めるようにする
```

| 書き方 | 意味 |
| --- | --- |
| `j.in` | 問題の入力（gen の出力）。読めないときは checker の失敗 |
| `j.out` | main の出力。読めないときは WA |
| `j.ref` / `j.has_ref()` | 愚直解の出力（`--ref` のときだけ）/ 渡されたかどうか |
| `read_int(lo, hi)` `read_u64()` `read_double(lo, hi)` `read_word(s)` `read_token()` | 1 トークン読む（[読み取り](#読み取り)） |
| `>>` | `cin` と同じ感覚で読む。値の範囲は自分で `ensure` する |
| `expect_eof()` `seek_eof()` | 残りが空白だけか確かめる（違えば失敗 / bool で返す） |
| `j.ensure(cond, msg)` `j.wrong(msg)` | WA |
| `j.out.fail(msg)` | WA。メッセージに今読んでいるトークンの番号が付く |
| `j.accept(msg)` | その場で AC にして終わる |
| `j.fail(msg)` `j.in.fail(msg)` | checker 自身の失敗（gen や checker のバグ） |

`judge` から何も言わずに戻れば AC。judge.hpp が include するのは `<string>` などの最低限だけなので、`vector` などは自分で include する。

## 呼ばれ方と終了コード

st は checker を次のように呼ぶ。

```bash
./checker <input> <output> [reference]
```

- `input` は in.txt、`output` は main の出力、`reference` は `--ref` を付けたときだけ渡る愚直解の出力。
- 標準入力は空、標準出力は捨てる。理由は標準エラーに書く（`j.wrong` などが書く）。
- 終了コードは 0 = AC、1 = WA、3 = checker の失敗。

st の判定はこうなる。

| checker の終わり方 | st の判定 |
| --- | --- |
| 終了コード 0 | OK |
| 終了コード 3、または checker の TL（`--checker-tl`、既定 10 秒）を超えた | CHECKER_FAIL。保存せずにそこで止め、終了コード 2 |
| それ以外（終了コード 1、その他の値、シグナルで落ちた） | WA。stderr の内容を理由として表示する（先頭の `WA: ` は取る） |

checker がバグで落ちたときも WA に見えるので、「WA の理由が変」なときは stderr_checker.txt を確かめる。

main が RE や TLE になったときは checker を呼ばない。`--ref` のときは愚直解の RE / TLE（REF_RE / REF_TLE）も先に判定する。

## 読み取り

`j.in`、`j.out`、`j.ref` はどれも同じ `Reader` で、空白（スペース、改行、`\r`、タブ）区切りのトークンを順に読む。行の区切りは見ない。

| 関数 | 読むもの | 失敗になるとき |
| --- | --- | --- |
| `read_int(lo, hi)` | 64 bit 整数。`lo` `hi` は省略可 | 整数でない、64 bit に入らない、`[lo, hi]` の外 |
| `read_u64()` | 符号なし 64 bit 整数 | 数字以外を含む（`+` も不可）、64 bit に入らない |
| `read_double(lo, hi)` | 実数（`strtod` で読める形。`1e-9` も可） | 実数でない、`inf` や `nan`、`[lo, hi]` の外 |
| `read_word(s)` | トークンをそのまま。`s` を渡すとそれと一致するか（大文字小文字は区別） | `s` と違う |
| `read_token()` | トークンをそのまま | 残りがない |
| `>>` | `int` `long long` `unsigned` 系 `double` `char` `std::string` | 読めない、型の範囲外。`char` は 1 文字のトークンだけ |
| `expect_eof()` | 残りが空白だけか | 空白以外が残っている |
| `seek_eof()` | 残りが空白だけなら `true` を返す | 失敗にはしない |
| `fail(msg)` | 何も読まずに失敗にする | いつでも |
| `is_open()` | 開いているか（`j.ref` 以外はいつも `true`） | 失敗にはしない |

どの関数も、残りがないときは「unexpected end of file」で失敗する。`read_int` は `+5` や `007` も読める（値として 5、7）。`>>` は `bool` には使えない（コンパイルエラー）。

失敗したときにどうなるかは、どこから読んだかで決まる。

| 読み先 | 失敗したとき |
| --- | --- |
| `j.out` | WA（main の出力がおかしい） |
| `j.in` `j.ref` | checker の失敗（gen や愚直解の出力が checker の想定と違う） |

メッセージは「`output (token 3): expected an integer, got "abc"`」のように、どれの何番目のトークンかが付く。番号は 1 から数えて、読もうとしたトークンの番号。

`--ref` を付けずに `j.ref` を読むと、checker の失敗になる。`--ref` の有無で振る舞いを変えたいときは `j.has_ref()` で確かめる。

## 判定

| 関数 | 終了コード | stderr に出るもの |
| --- | --- | --- |
| `j.ensure(cond, msg)` | `cond` が偽なら 1 | `WA: msg` |
| `j.wrong(msg)` | 1 | `WA: msg` |
| `j.accept(msg)` | 0 | `msg`（空なら何も出さない） |
| `j.fail(msg)` | 3 | `checker failure: msg` |
| 戻る | 0 | なし |

`wrong`、`accept`、`fail` はその場でプログラムを終える。`j.out.fail(msg)` は `j.wrong(msg)` と同じく WA だが、トークン番号が付くので「どこを読んでいるときにおかしいと分かったか」が残る。

## --ref で愚直解の出力を使う

checker の中で最適値を求めるのが重いときは、愚直解に最適値を出させて比べる。`--ref` を付けると st は guchoku.cpp も走らせ、その出力を 3 番目の引数で渡す。

例: 最長増加部分列の長さ L と、その添字を出力する問題。

```cpp
#include <vector>
#include "stress/judge.hpp"

void judge(Judge& j) {
    int n = j.in.read_int();
    std::vector<long long> a(n);
    for (auto& x : a) x = j.in.read_int();

    long long best = j.ref.read_int();            // 愚直解の L だけ読む（添字は読まなくていい）
    long long len = j.out.read_int(0, n);
    j.ensure(len == best, "length " + std::to_string(len) + " is not optimal (" + std::to_string(best) + ")");
    int prev = 0;
    for (int k = 0; k < len; k++) {
        int i = j.out.read_int(1, n);
        j.ensure(prev < i, "indices must be increasing");
        j.ensure(prev == 0 || a[prev - 1] < a[i - 1], "not increasing");
        prev = i;
    }
    j.out.expect_eof();
}
```

`j.ref` を最後まで読む必要はない。読まなかった分は無視される。

gen.cpp・main.cpp・guchoku.cpp まで揃った例は [examples/checker/lis](../examples/checker/lis/) にある。

## 例

どの例も、gen.cpp・main.cpp（・guchoku.cpp）まで揃ったものが [examples/checker](../examples/checker/) にあり、そのディレクトリで各 README のコマンド（`st -c checker.cpp`、`--ref` を使う例は `st -c checker.cpp --ref`）を打てば動く。

### Yes / No と構成

例: 和が S になる部分集合があれば `Yes` と添字を、なければ `No` を出力する問題。「ない」は checker だけでは確かめにくいので、愚直解の答えを使う（`--ref`）。

```cpp
#include <vector>
#include "stress/judge.hpp"

void judge(Judge& j) {
    int n = j.in.read_int();
    long long s = j.in.read_int();
    std::vector<long long> a(n);
    for (auto& x : a) x = j.in.read_int();

    bool exists = j.ref.read_word() == "Yes";
    std::string ans;
    j.out >> ans;
    if (ans == "No") {
        j.ensure(!exists, "answer exists but printed No");
    } else {
        j.ensure(ans == "Yes", "expected Yes or No, got " + ans);
        int k = j.out.read_int(1, n);
        std::vector<bool> used(n);
        long long sum = 0;
        for (int t = 0; t < k; t++) {
            int i = j.out.read_int(1, n) - 1;
            j.ensure(!used[i], "duplicate index");
            used[i] = true;
            sum += a[i];
        }
        j.ensure(sum == s, "sum is " + std::to_string(sum));
        if (!exists) j.fail("main found a subset but guchoku printed No");  // 愚直解のバグ
    }
    j.out.expect_eof();
}
```

### 実数

誤差だけの問題なら checker はいらず、`st --cmp float:1e-6` で足りる（[使い方](usage.md#比較方法)）。答えが一意でない実数の問題は checker で確かめる。

```cpp
#include <algorithm>
#include <cmath>
#include "stress/judge.hpp"

// x^2 = a となる x を 1 つ出力する（絶対誤差・相対誤差 1e-6 まで）
void judge(Judge& j) {
    double a = j.in.read_int();
    double x = j.out.read_double();
    j.ensure(std::abs(x * x - a) <= 1e-6 * std::max(1.0, a), "x^2 is too far from a");
    j.out.expect_eof();
}
```

## 今までの checker を使う

`./checker in out` の形で動き、AC なら終了コード 0 を返す checker は、judge.hpp を使っていなくてもそのまま `-c` に渡せる。`--ref` を付けると 3 番目の引数も渡る。上の[表](#呼ばれ方と終了コード)のとおり、終了コード 3 は checker の失敗、それ以外の 0 でない値は WA として扱う。

## main を自分で書く

judge.hpp は `main` を定義していて、`Judge` を作って `judge` を呼ぶ。`#define STRESS_NO_MAIN` を include の前に書くと `main` と `judge` の宣言がなくなるので、自分で書ける。

```cpp
#define STRESS_NO_MAIN
#include "stress/judge.hpp"

int main(int argc, char** argv) {
    stress::Judge j(argc, argv);
    // ...
    return 0;  // AC
}
```

## interactor との関係

`Reader`（読み取り）は interactor（[インタラクティブ問題](interactive.md)）と共通で、読み取りの関数と失敗の扱いは同じ。`it.in` は `j.in` と同じく interactor の失敗、`it.out` は `j.out` と同じく WA になる。

次のものは judge.hpp と interact.hpp の中で使っているだけで、checker から使うことは考えていない。

- `Reader::stream`: パイプから逐次読む Reader を作る（interactor の `it.out`）
- `judge_exit`、`kAccepted` `kWrong` `kJudgeFail`: 終了コードの定義と、メッセージを出して終わる関数
