# 平方根

x² = a となる実数 x を 1 つ出力する（絶対誤差・相対誤差 1e-6 まで）。x と -x のどちらも正解なので、`--cmp float` では比べられない。

- main.cpp は a の偶奇でわざと符号を変えている
- 愚直解はいらないので guchoku.cpp はない

```bash
st -c
```

説明は [docs/checker.md](../../../docs/checker.md#実数) にある。
