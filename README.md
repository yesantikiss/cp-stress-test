# cp-stress-test

競プロ用のランダムテスト（stress test）の道具。C++ の解答をランダムな入力で何度も動かし、間違う入力を見つける。

- 愚直解と出力を比べる。答えが複数ある問題はスペシャルジャッジ（checker）で判定する
- インタラクティブ問題も interactor を書けば試せる。適応的（adaptive）なジャッジも書ける
- 入力は gen.cpp に書く。配列、木、グラフ、文字列などを作る関数がある
- 小さい入力から試し、失敗したらもっと小さい入力を探し直してから保存する。seed で同じ入力を再現できる

## 必要なもの

- Python 3.11 以上（それより前は試していない）
- C++20 が通る g++
- Linux か WSL（macOS では試していない）

## 準備

好きな場所に clone して、`stress.py` を呼ぶエイリアスを作る。

```bash
git clone https://github.com/yesantikiss/cp-stress-test.git ~/cp-stress-test
# ~/.bashrc などに追加（パスは clone した場所に合わせる）
alias st='python3 ~/cp-stress-test/stress.py'
```

AtCoder Library や自作ライブラリを include するなら、場所を環境変数で教える。

```bash
export ACL_DIR=~/ac-library           # atcoder/ を含むディレクトリ（既定は問題のディレクトリの ac-library/）
export CP_LIBRARY_DIR=~/cp-library    # 既定は ~/cp-library
```

## 最初の 1 回

問題を解いているディレクトリに、解答の `main.cpp` と愚直解の `guchoku.cpp` を置く。`st init` で `gen.cpp` の雛形ができるので、問題の入力に合わせて書き換える。

```cpp
#include "stress/gen.hpp"

STRESS_GEN(random) {
    int n = g.size(1, 10);       // サイズは g.size で取る（小さい方から順に試される）
    g.out(n);
    g.out(g.vec(n, 1, 100));     // 1 以上 100 以下の整数を n 個
}
```

`st` で main と愚直解を 1000 回比べる。食い違うとその入力と両方の出力を表示して止まる。

```text
$ st
build ok (5.3s)
mode=compare/token gens=random base_seed=1 tl=2.0s

WA  token 1: main='0' ref='17'
case=10451216379200822465:1:random  ->  st repro 10451216379200822465:1:random
---- input ----
1
17
---- main ----
0
---- guchoku ----
17
saved: .stress/fail/20261104-234051-WA-random-910a2dec89025cc1
copied failing input -> in.txt
found after 1 cases (0.0s)
```

失敗した入力は `in.txt` にコピーされるので、そのまま main をデバッグできる。全部通れば終了コード 0、失敗を見つけたら 1 を返す。

## ドキュメント

- [使い方](docs/usage.md): `st` のコマンドとオプション、失敗の保存、ビルドの設定
- [gen.cpp の書き方](docs/gen.md): 生成器と `stress/gen.hpp` の関数
- [checker.cpp の書き方](docs/checker.md): スペシャルジャッジと `stress/judge.hpp`
- [インタラクティブ問題](docs/interactive.md): interactor.cpp と `st -i`
- [このライブラリのテスト](docs/testing.md): 何を確かめているか、CI、テストしていないもの

動く例は [examples/](examples) にある（checker と適応的な interactor）。

## まだできないこと

- 2 回実行する形式の問題（1 回目の出力を 2 回目の入力に渡す通信問題）
- 複数のケースを並列に回すこと

## テスト

```bash
bash tests/run_tests.sh
```

何を確かめているかは [docs/testing.md](docs/testing.md)。

## ライセンス

[CC0 1.0](LICENSE)。著作権を放棄しているので、改変や再配布も含めて断りなく自由に使ってよい。
