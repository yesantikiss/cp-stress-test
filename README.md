# cp-stress-test

競プロ用のランダムテスト（stress test）の道具。

- `stress/gen.hpp`: ジェネレータ用ヘッダ（seed 再現できる乱数、配列、木、グラフ、文字列、出力）
- `stress/judge.hpp`: スペシャルジャッジ（checker）用ヘッダ
- `stress/interact.hpp`: インタラクティブ問題の interactor 用ヘッダ
- `stress/adaptive.hpp`: 適応的（adaptive）な interactor を書くためのヘッダ
- `examples/adaptive/`: 適応的な interactor の例
- `stress.py`: ドライバ（ビルド、TL 付き実行、比較、失敗の保存、サイズの縮小）

## 準備

```bash
# ~/.bashrc
alias st='python3 $HOME/cp-stress-test/stress.py'
```

問題のディレクトリ（`~/atcoder`）で:

```bash
st init   # gen.cpp の雛形を置く
st        # main.cpp と guchoku.cpp を gen.cpp の入力で 1000 回比べる
```

## ドキュメント

- [使い方](docs/usage.md): `st` のコマンドとオプション、失敗の保存、ビルドの設定
- [gen.cpp の書き方](docs/gen.md): 生成器と `stress/gen.hpp` の関数
- [checker.cpp の書き方](docs/checker.md): スペシャルジャッジと `stress/judge.hpp`
- [インタラクティブ問題](docs/interactive.md): interactor.cpp と `st -i`

## テスト

```bash
bash tests/run_tests.sh
```
