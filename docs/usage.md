# 使い方

問題のディレクトリ（`~/atcoder`）で:

```bash
st init            # gen.cpp の雛形を置く（-c で checker.cpp も）
st                 # main.cpp と guchoku.cpp を gen.cpp の入力で 1000 回比べる
st -n 5000 --seed 42
st -g random -g all_same  # 使う生成器を絞る
st -t 60           # 60 秒回す
st -c checker.cpp  # スペシャルジャッジ（--ref で愚直解の出力を第 3 引数に渡す）
st --tl 1 --san    # TL 1 秒、ASan / UBSan / _GLIBCXX_DEBUG
st --cmp float:1e-6
st repro SEED:CAP:NAME    # 失敗時に表示されたケースを再現
st make max -n 5 -o cases/  # 入力ファイルだけ書き出す
st bench max --tl 2       # 最大ケースで main の実行時間を測る（TLE なら終了コード 1）
st last --to-in    # 直前の失敗を表示して in.txt にコピー（元の in.txt は .stress/in.txt.bak）
st clean           # 保存した失敗を消す
```

終了コード: 0 = 全部 OK, 1 = 失敗を見つけた, 2 = ビルドや引数のエラー。

- ケース k は seed = splitmix64(base + k)、生成器は順番、サイズ上限 cap は生成器ごとに 1 から 10 ケースずつ増やす。小さい入力から試すので、最初に見つかる失敗はたいてい小さい。
- 失敗したら、もっと小さい cap で失敗しないか探し直してから保存する（`--no-shrink` で無効）。
- 失敗は `.stress/fail/<日時>-<種類>-<seed>/` に入力、両方の出力、stderr、meta.json として残る。`in.txt` には触らない。
- ビルドは `-std=gnu++20 -O2 -DLOCAL`、ACL と `$CP_LIBRARY_DIR`（既定 `~/cp-library`）を include パスに入れる。ACL を include していて `pch_acl.hpp.gch` があれば使う。依存ファイルが変わったときだけ再ビルドする。
