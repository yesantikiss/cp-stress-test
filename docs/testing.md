# このライブラリのテスト

stress-test 自体が正しく動くかを確かめるテストの説明。自分の解答をランダムテストする使い方は [usage.md](usage.md)。

## 回し方

```bash
bash tests/run_tests.sh
```

全部で数分かかる。最後に `All tests passed` か `Some tests failed` が出て、失敗があれば終了コードが 1 になる。

C++ の単体テストだけを回すときは、ビルドして名前の一部を渡す。

```bash
g++ -std=gnu++20 -O2 -fsanitize=address,undefined -I. tests/gen_test.cpp -o /tmp/gen_test
/tmp/gen_test          # 全部
/tmp/gen_test tree     # 名前に tree を含むテストだけ
```

## CI

GitHub Actions（`.github/workflows/test.yml`）で、main への push と PR のたびに `tests/run_tests.sh` を回す。結果は PR の Checks と Actions のログに出る。

## 出力の読み方

テストごとに 1 行出る。C++ の単体テストは行頭にファイル名が付く。

```
gen_test: ok   tree_shapes
gen_test:   digraph_strong_shapes: gen_test.cpp:734: graph_has_shape(n, m, es, name, true, simple, true, true)
gen_test: FAIL digraph_strong_shapes (204 checks failed)
ok   checker_wa
FAIL tle (exit 0, want 1)
```

- C++ の単体テストは、失敗しても止まらずに最後まで回す。失敗した条件はテストごとに最初の 5 個まで、ファイル名と行番号と式で出る
- `st` のテストは終了コードを比べる。失敗したら、そのコマンドの出力の最後の 20 行を出す

## テストの一覧

| ファイル | 確かめること |
| --- | --- |
| `tests/gen_test.cpp` | `stress/gen.hpp` の各関数が、オプションの意味どおりの値を返すか |
| `tests/adaptive_test.cpp` | `stress/adaptive.hpp` の候補の列挙と、返事の選び方 |
| `tests/run_tests.sh` の残り | `st`（`stress.py`）を、わざと間違えた解で動かしたときの判定と後始末 |

### gen_test

乱数で作った値を、性質を満たすかどうかで判定する。seed は固定なので、何度回しても同じ結果になる。

| テスト | 確かめること |
| --- | --- |
| `seed` | 同じ seed なら同じ出力 |
| `range` / `size_cap` | 範囲に収まる、端の値が出やすい、`size` が cap で切られる |
| `decimal` | 範囲、小数点以下の桁数、`.exact` |
| `distinct_perm_partition` / `sorted` | 相異なる、順列になる、和が合う、`.strict` / `.desc` |
| `seq_shapes` / `seq_mixed` | 形ごとの性質（下の表）。mixed はタグの形と中身が合うか |
| `tree_shapes` / `tree_mixed` / `parents` / `parents_mixed` | 木になっている、形ごとの性質、`.ordered`、0 始まり |
| `tree_options` | `.w`、`.ordered`、`one_indexed = false` |
| `graph_basic` / `graph_shapes` / `graph_mixed` | 本数、範囲、単純性、連結性、`.ordered`、形ごとの性質 |
| `digraph_shapes` / `digraph_strong_shapes` / `digraph_mixed` | 上に加えて `.reach`（weak / root / strong を到達できる頂点数で確認） |
| `graph_big` | 大きいグラフや密なグラフでも終わる |
| `dag` | 閉路なし、`.increasing`、`.reach` |
| `bipartite` | `.label` 3 種の番号の範囲、2 彩色できる、`.ordered`、連結性 |
| `weights` | `.w` を付けないと重みなし。Range なら範囲内、ラムダなら辺 1 本につき 1 回呼ぶ |
| `str_shapes` / `str_mixed` / `str_others` | 長さ、文字種、形ごとの性質、`strs` / `mutate` / `grid` |
| `big` / `big_conversion` | 桁数、先頭の 0、範囲、進数の変換、`2^60-1` などの書き方 |
| `output` / `tags` | `g.out` の書式、タグの付き方 |
| `gen_errors` | 矛盾した指定が gen error（終了コード 3）になる |

形ごとの性質は「その形なら必ず成り立つ条件」で見ている。条件を満たしても、その形だと言い切れるわけではない。判定がゆるすぎないかは、ランダムな木やグラフや文字列が判定を通らないことで確かめている。

| 形 | 見ている条件 |
| --- | --- |
| 木 path | 最大次数 2 以下 |
| 木 star | 次数 n-1 の頂点がある |
| 木 caterpillar | 葉を除くとパス |
| 木 binary | 最大次数 3 以下、直径 2 floor(log2 n) 以下 |
| 木 deep | 直径 ceil((n-1)/3) 以上 |
| グラフ pieces | 弱連結成分が 2 つ以上 |
| グラフ bipartite | 2 彩色できる |
| 有向グラフ dag | 閉路なし |
| グラフ path | 隣接頂点数 2w 以下、連結なら直径 (n-1)/w 以上（w は辺を張る番号の差の上限） |
| グラフ hub | 疎なとき、3 頂点で辺の 6 割以上を覆う |
| 文字列 periodic | ある周期で、ずれる位置が 4 個以下 |
| 文字列 palindrome | 回文 4 個以下に分けられる |
| 文字列 fibonacci | 2 種類の文字を入れ替えるとフィボナッチ文字列の部分文字列 |
| 文字列 runs | 長さ 60 以上なら、連の個数が長さの 1/3 以下 |
| 配列 same / few / asc / desc / edge | 1 種類、3 種類以下、広義増加、広義減少、両端の値だけ |

random の形は範囲などの共通の条件だけを見る。

### adaptive_test

| テスト | 確かめること |
| --- | --- |
| `candidates` / `trees` | `cand::*` の個数と中身。木は n^(n-2) 個で、どれも木 |
| `binary_search` | 数当てで、ちょうど ceil(log2 n) 回かかる |
| `tie_by_seed` | 返事が同数なら seed で選び、同じ seed なら同じ |
| `many_replies` / `fixed` / `state` | 返事の種類が多いとき、`Strategy::fixed`、`AdaptiveState` |

### st のテスト

`tests/sample`、`tests/multi`、`tests/interactive` に、正しい解とわざと間違えた解（WA / RE / TLE、flush 忘れ、クエリ回数超過、余分な出力など）を置き、`st` の終了コードと保存されるファイルを確かめる。

- 判定: OK / WA / RE / TLE、checker（`-c`、`--ref`）、interactor（`-i`、適応的な interactor）
- 再現: `st repro` で同じ入力が出る
- in.txt: 失敗時の書き出し、手書きの in.txt の退避、`--no-to-in`、`st restore`
- 生成器: `-g` での選択、STRESS_BIG の回数、`st make`、`st bench`、タグの表示
- `examples/adaptive/` と `examples/checker/` の例が、そのまま OK で通る

## テストしていないもの

- 乱数の分布の偏り（端が出やすいこと以外）
- 縮小（shrink）が最小の入力まで縮むか
- docs のコード片（`examples/` にあるものは上のテストで回る）
- 利用者の環境（WSL、ACL の置き場所、PCH）での動作

## 既知の失敗

- `digraph_strong_shapes`: `g.digraph` で `.reach = Reach::strong` にすると、土台を耳分解で作るので path / hub の形にならない。直すまでこのテストは落ちる
