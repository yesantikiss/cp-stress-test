# 使い方

問題のディレクトリ（`~/atcoder`）で `st` を使う。`st` は `python3 ~/cp-stress-test/stress.py` の alias（[README](../README.md)）。

```bash
st init            # gen.cpp の雛形を置く（-c で checker.cpp、-i で interactor.cpp も）
st                 # main.cpp と guchoku.cpp を gen.cpp の入力で 1000 回比べる
st -n 5000 --seed 42
st -g random -g all_same  # 使う生成器を絞る
st -t 60           # 60 秒回す
st -c checker.cpp  # スペシャルジャッジ（--ref で愚直解の出力を第 3 引数に渡す）
st -i interactor.cpp  # インタラクティブ（docs/interactive.md）
st --tl 1 --san    # TL 1 秒、ASan / UBSan / _GLIBCXX_DEBUG
st --cmp float:1e-6
st repro SEED:CAP:NAME    # 失敗時に表示されたケースを再現
st make max -n 5 -o cases/  # 入力ファイルだけ書き出す
st bench max --tl 2       # 最大ケースで main の実行時間を測る（TLE なら終了コード 1）
st last --to-in    # 直前の失敗を表示して in.txt にコピー（元の in.txt は .stress/in.txt.bak）
st clean           # 保存した失敗を消す
```

| コマンド | 内容 |
| --- | --- |
| `st` / `st run` | ランダムテストを回す |
| `st repro` | 失敗したケースを 1 つ再現する |
| `st make` | 入力ファイルだけ書き出す |
| `st bench` | 生成した入力で main の実行時間を測る |
| `st last` | 直前の失敗を表示する |
| `st clean` | `.stress/` の失敗ケースを消す |
| `st init` | 雛形を置く |

`st` の後がコマンド名でなければ `st run` として扱う。オプションの一覧は `st --help`（`st run` のもの）や `st make --help` などで出る。

## 基本の流れ

1. `st init` で gen.cpp を置き、入力の生成を書く（[gen.cpp の書き方](gen.md)）。
2. main.cpp と guchoku.cpp を書いて `st`。全部一致すれば `OK  1000 cases  max 3ms` のように出る。
3. 失敗すると、判定、入力、両方の出力、stderr と、再現用のコマンドを出して止まる。gen がタグを付けていれば `tags:` の行も出る（[タグ](gen.md#タグ)）。

```text
WA  token 1: main='5' ref='6'
case=9473625110285193:3:random  ->  st repro 9473625110285193:3:random
tags: str=palindrome
---- input ----
...
saved: .stress/fail/20261103-151233-WA-random-0021a6c1f0e4b5e9   (st last --to-in で in.txt にコピー)
```

4. 直したら `st repro 9473625110285193:3:random` で同じケースを確かめる。手元の実行で追いたいときは `st last --to-in` で in.txt にコピーする。

## st run

| オプション | 内容 |
| --- | --- |
| `-n N` `--iters N` | 回数（既定 1000） |
| `-t SEC` `--time SEC` | 回数の代わりに秒数で回す |
| `--big-iters N` | `-n` も `-t` も書かないとき、STRESS_BIG の生成器 1 つあたりの回数（既定 20）。STRESS_BIG だけを回すときは合計もこの回数で止まる |
| `--seed N` | 基準 seed（省略時はランダム。最後に `base_seed=...` と出る） |
| `-g NAME` `--gen NAME` | 使う生成器。複数書ける、カンマ区切りも可。省略時は STRESS_BIG 以外を全部 |
| `--max-cap N` | cap の上限（[seed と cap](#seed-と-cap)） |
| `--no-shrink` | 失敗したあと縮小しない |
| `--shrink-tries N` | 縮小で cap 1 つあたりに試す回数（既定 200） |
| `--shrink-time SEC` | 縮小にかける時間の上限（既定 20 秒） |

次のオプションは `st run` と `st repro` の両方で使える。

| オプション | 内容 |
| --- | --- |
| `--main SRC` | 解答（既定 main.cpp） |
| `--guchoku SRC` | 愚直解（既定 guchoku.cpp） |
| `-c SRC` `--checker SRC` | checker で判定する（[checker.cpp の書き方](checker.md)）。愚直解は `--ref` のときだけ使う |
| `--ref` | checker モードでも愚直解を走らせ、出力を checker の第 3 引数で渡す |
| `-i SRC` `--interactor SRC` | インタラクティブ（[インタラクティブ問題](interactive.md)）。`-c` `--ref` `--cmp` とは一緒に使えない |
| `--cmp MODE` | 比較方法（[比較方法](#比較方法)） |
| `--tl SEC` | main の制限時間（既定 2 秒） |
| `--ref-tl SEC` | 愚直解の制限時間（既定 10 秒） |
| `--checker-tl SEC` | checker と interactor の制限時間（既定 10 秒） |
| `--gen-tl SEC` | gen の制限時間（既定 10 秒） |
| `--gen-file SRC` | 生成器のソース（既定 gen.cpp） |
| `--san` `--cxxflags FLAGS` | [ビルド](#ビルド)の設定 |

制限時間は実時間で測る。

### 判定の種類

| 判定 | 意味 | 保存 |
| --- | --- | --- |
| WA | 愚直解と出力が違う、checker / interactor が WA | する |
| RE | main が 0 以外で終わった、シグナルで落ちた | する |
| TLE | main が `--tl` を超えた | する |
| REF_RE / REF_TLE | 愚直解が落ちた / `--ref-tl` を超えた | する |
| GEN_FAIL | gen が落ちた / `--gen-tl` を超えた | しない |
| CHECKER_FAIL / INTERACTOR_FAIL | checker / interactor が終了コード 3 で終わった、TL を超えた（interactor は落ちたときも） | しない |

保存しない 3 つは道具の側のバグなので、縮小せずにその場で止まり、終了コード 2 を返す。

## seed と cap

- ケース k（0 始まり）の seed は `splitmix64(base + k)`。
- 生成器が複数あれば、ケースごとに順番に使う（1 つ目、2 つ目、...、1 つ目、...）。
- cap（`g.size()` の上限）は生成器ごとに 1 から始めて、その生成器を 10 回使うたびに 1 増やす。`--max-cap` を付けるとそこで止まる。
- 小さい入力から試すので、最初に見つかる失敗はたいてい小さい。

`--seed` を同じにすれば同じケースの列になる（gen.cpp と生成器の数が同じなら）。

### 縮小

失敗した cap が 2 以上なら、同じ生成器でもっと小さい cap を試して、失敗する入力を探し直す（縮小）。

- 試す cap は 1、2、3、4、5、6、8、10、12、16、20、25、32、... のうち元の cap より小さいものと、元の cap の 1/2 と 3/4。小さい方から試す。
- cap 1 つにつき `--shrink-tries` 回（既定 200）、別の seed で試す。最初に失敗したものを残す。
- 全体で `--shrink-time` 秒（既定 20）を超えたら打ち切り、元の失敗を残す。
- 縮小で見つかった失敗は判定の種類が元と違うこともある（WA で見つけて、小さい cap では RE など）。

`--no-shrink` で縮小しない。

## 比較方法

`--cmp` で main と愚直解の出力の比べ方を選ぶ。checker や interactor を使うときは使わない。

| MODE | 比べ方 |
| --- | --- |
| `token`（既定） | 空白（スペース、改行など）で区切ったトークンの列が同じか。空白の量や行末の違いは無視する |
| `exact` | バイト列が完全に同じか。違うときは最初に違うバイトの位置を出す |
| `float` `float:EPS` | トークンごとに比べ、どちらも実数として読めれば `abs(main - ref) <= EPS * max(1, abs(ref))` なら同じとみなす（絶対誤差か相対誤差が EPS 以下）。EPS は省略時 1e-6。実数として読めないトークンは文字列として比べる |

WA の理由には、最初に違ったトークンの番号と両方の値、またはトークン数の違いが出る。

## st repro

```bash
st repro SEED:CAP:NAME [run と同じオプション]
```

- 失敗時の `case=...` をそのまま渡す。CAP を `-` にすると上限なし、NAME を省略すると先頭の生成器を使う。
- 再現すれば失敗として保存し、表示する（終了コード 1）。再現しなければ `OK ... (再現しなかった)` と入力の場所を出す（終了コード 0）。
- 失敗時に表示される `st repro ...` には `-c` / `-i` だけが付く。`--main`、`--ref`、`--cmp`、`--tl` などを付けて回していたときは、同じものを自分で付ける。

## st make

```bash
st make [NAME ...] [-n N] [--seed S] [--cap C] [-o DIR]
```

生成器の入力ファイルだけを書き出す。main や愚直解はビルドしない。

| オプション | 内容 |
| --- | --- |
| `NAME ...` | 生成器の名前。省略時は STRESS_BIG のもの、なければ全部 |
| `-n N` `--count N` | 生成器ごとの個数（既定 5） |
| `--seed S` | 基準 seed（省略時はランダム） |
| `--cap C` | cap（省略時は上限なし） |
| `-o DIR` `--out DIR` | 出力先（既定 `cases/`） |

ファイル名は `<生成器>-01.txt`、`<生成器>-02.txt`、...。同じ名前のファイルは上書きする。ビルドのオプション（`--gen-file`、`--san`、`--cxxflags`、`--gen-tl`）も使える。

## st bench

```bash
st bench [NAME ...] [-n N] [--seed S] [--cap C] [--tl SEC] [--main SRC]
```

生成した入力で main だけを走らせ、ケースごとの実行時間を出す。生成器の選び方、`--seed`、`--cap` は `st make` と同じで、`-n` の既定は 3。

- `--tl` の 8 割を超えたケースは黄色で出す。
- TLE か RE が 1 つでもあれば終了コード 1、なければ一番遅かった時間を出して 0。
- 使った入力は `.stress/bench/` に残る（次の `st bench` で消える）。

## st last

直前に保存した失敗（`.stress/last`）を、走らせたときと同じ形で表示する。

`--to-in` を付けると、失敗した入力を in.txt にコピーする。in.txt が空でなければ、先に `.stress/in.txt.bak` へ退避する（退避は 1 つだけで、次の `--to-in` で上書きされる）。

## st clean

`.stress/` の fail、run、last、bench、make を消す。`--all` を付けるとビルド済みのバイナリ（`.stress/bin/`）も消す。

## st init

stress-test の `templates/` から雛形をコピーする。

```bash
st init            # gen.cpp
st init -c         # gen.cpp と checker.cpp
st init -i         # gen.cpp と interactor.cpp
st init gen2.cpp   # gen.cpp の雛形を gen2.cpp という名前で置く
```

- 同じ名前のファイルがあれば飛ばす。`--force` で上書きする。
- 名前を指定できるのは、置くファイルが gen.cpp だけのとき。`-c` や `-i` と一緒に書いた名前は無視する。

## ビルド

main、愚直解、gen、checker、interactor を並列にビルドする。

- フラグは `-std=gnu++20 -O2 -DLOCAL`。`--san` を付けると `-fsanitize=address,undefined -fno-omit-frame-pointer -g -D_GLIBCXX_DEBUG` を足す。
- `--cxxflags "..."` か環境変数 `STRESS_CXXFLAGS` で、さらにフラグを足せる（`--cxxflags` が優先）。
- include パスには、stress-test のディレクトリ（`stress/gen.hpp` などを読むため）、ACL（問題のディレクトリか `~/atcoder` の `ac-library/`）、`$CP_LIBRARY_DIR`（既定 `~/cp-library`）を入れる。
- 問題のディレクトリに `pch_acl.hpp.gch` があり、ソースが `#include <atcoder/...>` を含むときは、`-include pch_acl.hpp` を付けてプリコンパイル済みヘッダを使う。main と愚直解だけで、`--san` のときは使わない。
- バイナリは `.stress/bin/<種類>-<ハッシュ>` に置く。ハッシュはソースのパスとフラグから作るので、`--san` の有無などで別のバイナリになる。
- ソースか、それが include しているファイルが変わったときだけ再ビルドする。

ビルドに失敗したらエラーを出して終了コード 2 で止まる。

## .stress/ の中身

| パス | 内容 |
| --- | --- |
| `fail/<日時>-<判定>-<生成器>-<seed>/` | 保存した失敗。seed は 16 進 |
| `last` | 直前の失敗へのシンボリックリンク |
| `run/` | 今走らせているケースの作業場所 |
| `bench/` | `st bench` で使った入力 |
| `bin/` | ビルド済みのバイナリと依存ファイル |
| `in.txt.bak` | `st last --to-in` で退避した in.txt |

失敗のディレクトリには、あるものだけ次のファイルが入る。

| ファイル | 内容 |
| --- | --- |
| `in.txt` | 入力 |
| `out_main.txt` `out_ref.txt` | main と愚直解の出力 |
| `stderr_main.txt` `stderr_ref.txt` `stderr_gen.txt` | それぞれの stderr |
| `stderr_checker.txt` `stdout_checker.txt` | checker の出力 |
| `transcript.txt` `stderr_interactor.txt` | インタラクティブのやり取りと interactor の stderr |
| `tags.txt` | gen が付けたタグ（`タグ<TAB>回数` を 1 行ずつ）。タグがあるときだけ |
| `meta.json` | 判定、理由、seed、cap、生成器、タグ、再現コマンド、モード、比較方法、TL、ソースのパスと SHA-1、日時 |

st は問題のディレクトリの in.txt には触らない（`st last --to-in` のときだけ書く）。

## 終了コード

| コード | 意味 |
| --- | --- |
| 0 | 全部 OK（`st repro` では再現しなかった） |
| 1 | 失敗を見つけた（`st bench` では TLE か RE があった） |
| 2 | ビルドや引数のエラー、GEN_FAIL、CHECKER_FAIL、INTERACTOR_FAIL |
| 130 | Ctrl+C で止めた |
