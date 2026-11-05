# 部分和

和が S になる部分集合があれば `Yes`、個数、添字を、なければ `No` を出力する。

- 「ない」は checker だけでは確かめられないので、guchoku.cpp（全探索）の Yes / No を `j.ref` で読む
- `Yes` のときは添字の重複と和を checker で確かめる
- main が見つけたのに guchoku が `No` なら、愚直解のバグとして `j.fail` にする

```bash
st -c --ref
```

説明は [docs/checker.md](../../../docs/checker.md#yes--no-と構成) にある。
