#pragma once

// スペシャルジャッジ（checker）用。
//
//   #include "stress/judge.hpp"
//   void judge(Judge& j) {
//       int n = j.in.read_int();                 // 問題の入力
//       long long x = j.out.read_int(0, 1e18);   // 解答の出力（形式違反は WA）
//       j.ensure(x % n == 0, "x is not a multiple of n");
//       j.out.expect_eof();
//   }
//
// 実行: ./checker <input> <output> [reference]
//   終了コード 0 = AC, 1 = WA, 3 = checker 自身の失敗。理由は stderr に出る。
//   reference はドライバの --ref で愚直解の出力が渡されたときだけ存在する。

#include "reader.hpp"

namespace stress {

class Judge {
  public:
    Reader in, out, ref;

    Judge(int argc, char** argv) {
        if (argc < 3) judge_exit(kJudgeFail, std::string("usage: ") + argv[0] + " <input> <output> [reference]");
        in = Reader(argv[1], "input", kJudgeFail);
        out = Reader(argv[2], "output", kWrong);
        if (argc >= 4) ref = Reader(argv[3], "reference", kJudgeFail);
    }

    bool has_ref() const { return ref.is_open(); }

    void ensure(bool cond, const std::string& msg) {
        if (!cond) wrong(msg);
    }
    [[noreturn]] void wrong(const std::string& msg) { judge_exit(kWrong, "WA: " + msg); }
    [[noreturn]] void accept(const std::string& msg = "") { judge_exit(kAccepted, msg); }
    [[noreturn]] void fail(const std::string& msg) { judge_exit(kJudgeFail, "checker failure: " + msg); }
};

}  // namespace stress

using stress::Judge;

#ifndef STRESS_NO_MAIN
void judge(stress::Judge& j);

int main(int argc, char** argv) {
    stress::Judge j(argc, argv);
    judge(j);
    return stress::kAccepted;  // judge() が何も言わずに戻ったら AC
}
#endif
