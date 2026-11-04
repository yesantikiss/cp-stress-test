# 最長増加部分列

長さ n の数列 a の（狭義）最長増加部分列の長さ L と、その添字を 1 つ出力する。添字の選び方はいくつもあるので checker で確かめる。

- 最適な L は checker の中では求めず、guchoku.cpp（全探索）の出力を `j.ref` で読む
- 添字が増加しているか、値が増加しているかを checker で確かめる

```bash
st -c checker.cpp --ref
```

説明は [docs/checker.md](../../../docs/checker.md#--ref-で愚直解の出力を使う) にある。
