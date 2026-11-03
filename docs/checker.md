# checker.cpp

```cpp
#include "stress/judge.hpp"

void judge(Judge& j) {
    long long n = j.in.read_int();
    long long x = j.out.read_int(1, n);   // 形式違反・範囲外は自動で WA
    j.ensure(x % 2 == 0, "x must be even");
    j.out.expect_eof();
}
```

`./checker <input> <output> [reference]`、終了コード 0 = AC, 1 = WA, 3 = checker の失敗。今までの `checker in out` 形式の checker もそのまま使える。

`j.out >> x` のように `>>` でも読める（読めない、型の範囲外のときは WA。値の範囲は自分で `ensure` する）。
