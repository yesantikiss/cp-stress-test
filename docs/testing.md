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

- C++ の単体テストは失敗しても止まらずに最後まで回す。失敗した条件はテストごとに最初の 5 個まで、ファイル名と行番号と式で出る
- `st` のテストは終了コードを比べる。失敗を保存する場合は、meta.json の判定の種類（WA / RE / TLE など）も比べる（`<名前>_kind` の行）。失敗したら、そのコマンドの出力の最後の 20 行を出す

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
| `one_indexed` | 0 始まりにすると、全関数で同じ seed の 1 始まりの番号からちょうど 1 引いたものになる。途中で切り替えるとそれ以降だけ変わる |
| `graph_basic` / `graph_shapes` / `graph_mixed` | 本数、範囲、単純性、連結性、`.ordered`、形ごとの性質 |
| `digraph_shapes` / `digraph_strong_shapes` / `digraph_mixed` | 上に加えて `.reach`（weak / root / strong を到達できる頂点数で確認） |
| `graph_big` | 大きいグラフや密なグラフでも終わる（hub で中心の辺が尽きる本数も含む） |
| `dag` | 閉路なし、`.increasing`、`.reach` |
| `bipartite` | `.label` 3 種の番号の範囲、2 彩色できる、`.ordered`、連結性 |
| `weights` | `.w` を付けないと重みなし。Range なら範囲内、ラムダなら辺 1 本につき 1 回呼ぶ |
| `str_shapes` / `str_mixed` / `str_others` | 長さ、文字種、形ごとの性質、`strs` / `mutate` / `grid` |
| `big` / `big_conversion` | 桁数、先頭の 0、範囲、進数の変換、`2^60-1` などの書き方 |
| `prime` | 素数判定を 10^6 までのふるいと比べる。大きい素数と強擬素数、範囲内の素数が端も含めて全部出る、上に無いときは下を探す、`long long` の上端 |
| `highly_composite` | 10^6 までは約数の個数を数えた結果と一致、10^18 以下で 156 個、最大が 897612484786617600、`long long` 全体で約数の個数が増え続ける |
| `output` / `tags` | `g.out` の書式（`std::vector<bool>` を含む）、タグの付き方 |
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
| グラフ hub | 疎なとき、3 頂点で辺の 6 割以上を覆う（strong は `m >= 2(n-1)` のときだけ） |
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
| `to_text` | `std::string_view` を含む文字列化が 1 文字ずつに分解されないこと |
| `reader` | `Reader >>` の `long double` 精度と `float` 範囲外、`read_u64` の先頭ゼロと範囲外 |

### st のテスト

`tests/sample`、`tests/multi`、`tests/interactive`、`tests/shrink`、`tests/limits`、`tests/leftover` に、正しい解とわざと間違えた解（WA / RE / TLE、flush 忘れ、クエリ回数超過、余分な出力など）や、わざと壊した愚直解、gen、checker を置き、`st` の終了コード、判定の種類、保存されるファイルを確かめる。

- 判定: OK / WA / RE / TLE / OLE / REF_RE / REF_TLE / REF_OLE、checker（`-c`、`--ref`）、interactor（`-i`、適応的な interactor）
- 道具の側の失敗: gen error は GEN_FAIL、checker が例外で落ちたら CHECKER_FAIL で、どちらも終了コード 2（`st repro` でも終了コード 2 で、`.stress/fail` に保存せず in.txt も変えない）
- 引数とヘルプ: `--tl` `--ref-tl` `--checker-tl` `--gen-tl` `--output-limit` `-t` `--shrink-time` の 0 以下や `nan` / `inf`、`--max-cap` `--cap` `-n` `--shrink-tries` の 0 以下が終了コード 2 になり `in.txt` を変えないこと。`st --help` にサブコマンドの一覧が出ること
- 比較方法: `--cmp exact`、`--cmp float:EPS`（誤差の内と外、`0`、不正な指定で終了コード 2）、token で小数がずれたら WA
- インタラクティブ: interactor の WA の直後に main が落ちても、10 回とも WA になる。`it.send(std::string_view(...))` が 1 つの文字列として送られる。改行なしで数十 MB 出力する main でも TLE で止まり `st` のメモリが増え続けないこと（`Transcript.feed` の単体チェックも含む）。`report_interactor.txt` の壊れた行の扱い
- 縮小: 縮小中に gen error が出ても本物の WA を残し、警告を出す。縮小中に Ctrl+C（SIGINT）を送ると、元の WA を保存して終了コード 1（縮小がすぐ終わらないよう、1 回ごとに待つ `main_slow.cpp` で確かめる）。`n >= 20` でだけ落ちる `main_n20.cpp` で大きい cap から 2 段階縮小により cap 20 前後まで縮むこと。2 段階縮小の単体チェック（1 回目・2 回目の動作、中断時の復元、`-j 1` と `-j 4` の一致、ステータス表示の間引き）
- 実行環境: 深さ 100 万の再帰が `st` でも `st bench` でも落ちない。出力し続ける main が `--output-limit` で止まって OLE（愚直解なら REF_OLE、`st bench` でも OLE）になり、保存された出力も上限以下。Ctrl+C で止めたあとに main が残らない
- ビルド: リンクエラーの見分け（静的リンクのやり直し用）、作業ディレクトリの中身だけ消す `fresh_dir`、作業場所を `/dev/shm` に置いて `.stress/run` をリンクにすること（使えなければ `.stress/run`）、`--cxxflags` のクォート、`--cxxflags=-O0` の形、不正なクォートのエラー、空白や特殊文字を含む依存パス（`.d`）のキャッシュと更新検知、`pch_acl.hpp` 本体が無く `.gch` のフラグが合わないときの PCH なしやり直し、そのあと静的リンクなしでもやり直すとき `-include` が残らないこと（g++ は偽物）、日本語のリンクエラーも見分けること、最後に各テストのディレクトリで `st clean` を呼んで `/dev/shm` 側も消えること
- 再現: `st repro` で同じ入力が出る。失敗時に表示された `st repro ...` に `--main` `--cmp` `--tl` `--cxxflags=...` `--no-leftover` が付き、そのまま打てば再現する
- in.txt: 失敗時の書き出し、手書きの in.txt の退避、`--no-to-in`、`st restore`
- ジェネレーター: `-g` での選択（重複のまとめ、空指定）、`--big-iters`（`0` と負値）、STRESS_BIG の回数と終了後に次のジェネレーターを飛ばさないこと、`st make`（作業ディレクトリ外への `-o` 絶対パスも含む）、`st bench`、タグの表示
- 並列実行（`-j`）: 同じ seed なら `-j 1` と `-j 4` で、保存されるケースと `found after N cases` が同じ（WA、RE、縮小あり・なし、checker、インタラクティブ）。並列でも子プロセスの上限が付く（OLE、深い再帰）、Ctrl+C で main が残らない、縮小中の gen error と Ctrl+C、STRESS_BIG の順番、本物の TLE はそのまま TLE、ほかの main と同時に動くと遅くなる main では単独で測り直して `-j 1` に切り替え、`-j -1` は終了コード 2、`STRESS_JOBS` が既定になり `-j` が優先、不正な `STRESS_JOBS` は終了コード 2、`STRESS_JOBS` があっても `st repro` は動く。偽の evaluate で、測り直しのために止めて戻したケースを取りこぼさないこと（it が尽きたあと、後ろの失敗が決まったあとの両方）と、単独で WA になったときの警告の文言
- 入力の読み残し（`tests/leftover`）: `-include stress/leftover.hpp` で、`cin`、`sync_with_stdio(false)` の `cin`、`scanf`、`sync_with_stdio(false)` のまま `scanf` で読んで `exit` で終わるもの、の 4 通りを、読み残しあり（T を n と読み違える、行の途中まで、64 KB を超える入力の最後の行、長い 1 トークンとちょうど 5 トークン）、なし（末尾の空白と空行だけ、大きい入力）、パイプで渡したとき（何も出ない）で確かめ、行番号と残りの行数と先頭のトークンが合うこと。PCH の後ろに差し込んでも PCH が使われること。グローバルな `int stat[10];` を持つ `main.cpp` が `st` でビルドして動くこと。`st` では、件数と例の表示（main と愚直解、`-j 3` でも直列と同じ例）、正しく読む解では出ないこと、`--no-leftover`、checker モード（main だけ）、`st repro`、WA のときも出ること、`st bench`、`-i` では出ないこと
- `examples/adaptive/` と `examples/checker/` の例がそのまま OK で通る

## テストしていないもの

- 乱数の分布の偏り（端が出やすいこと以外）
- 縮小（shrink）が最小の入力まで縮むか
- docs のコード片（`examples/` にあるものは上のテストで回る）
- 利用者の環境（WSL や実際の ACL の置き場所・構成）での動作（最小構成での PCH フォールバック自体は上でテストしている）
