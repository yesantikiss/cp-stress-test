// st -i 用の interactor。it.in は gen の出力（隠し情報も含む）、it.out は main の出力。
// it.send(...) で main に 1 行送る（改行と flush は自動）。
// 読めなければ自動で WA。it.wrong("理由") で WA、何事もなく戻れば AC
// （戻ったあと main が余計な出力をせずに終わるかも確かめる）。
#include "stress/interact.hpp"

void interact(Interactor& it) {
    // 例: 1 以上 n 以下の隠した数 x を 25 回以内の質問で当てる
    int n, x;
    it.in >> n >> x;
    it.send(n);
    it.query_limit(25);
    while (true) {
        std::string t;
        it.out >> t;
        if (t == "?") {
            int y;
            it.out >> y;
            it.ensure(1 <= y && y <= n, "y is out of range");
            it.count_query();
            it.send(x <= y ? "Yes" : "No");
        } else if (t == "!") {
            int ans;
            it.out >> ans;
            it.ensure(ans == x, "wrong answer: " + std::to_string(ans));
            return;
        } else {
            it.wrong("unknown command: " + t);
        }
    }
}
