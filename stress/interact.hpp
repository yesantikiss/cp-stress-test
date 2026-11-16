#pragma once

// インタラクティブ問題の interactor 用。
//
//   #include "stress/interact.hpp"
//   void interact(Interactor& it) {
//       int n, x;
//       it.in >> n >> x;                 // gen の出力（隠し情報も含む）
//       it.send(n);                      // main へ 1 行送る（改行と flush は自動）
//       it.query_limit(25);
//       while (true) {
//           std::string t;
//           it.out >> t;                 // main の出力を読む（読めなければ WA）
//           if (t == "?") {
//               int y;
//               it.out >> y;
//               it.ensure(1 <= y && y <= n, "y out of range");
//               it.count_query();        // 制限を超えたら WA
//               it.send(x <= y ? "Yes" : "No");
//           } else if (t == "!") {
//               int ans;
//               it.out >> ans;
//               it.ensure(ans == x, "wrong answer");
//               return;                  // 戻ったら、main が余計な出力をせずに終われば AC
//           } else {
//               it.wrong("unknown command: " + t);
//           }
//       }
//   }
//
// 実行: ./interactor <input>  （標準入力 = main の出力、標準出力 = main の入力）
//   終了コード 0 = AC, 1 = WA, 3 = interactor 自身の失敗。理由は stderr に出る。
//   ドライバ（st -i）が間に入ってやり取りを transcript.txt に残す。

#include <csignal>
#include <sstream>
#include <vector>

#include "reader.hpp"

namespace stress {

class Interactor {
  public:
    Reader in, out;

    Interactor(int argc, char** argv) {
        if (argc < 2) judge_exit(kJudgeFail, std::string("usage: ") + argv[0] + " <input>");
        std::signal(SIGPIPE, SIG_IGN);  // main が先に終わっても、書き込みエラーとして扱う
        in = Reader(argv[1], "input", kJudgeFail);
        out = Reader::stream(0, "output", kWrong);
        std::atexit(&Interactor::report);
    }
    Interactor(const Interactor&) = delete;
    Interactor& operator=(const Interactor&) = delete;

    // main へ 1 行送る。引数は空白区切り、vector は要素を空白区切りで並べる。
    template <class... Ts>
    void send(const Ts&... xs) {
        std::ostringstream os;
        os.precision(17);
        bool first = true;
        (put(os, xs, first), ...);
        os << '\n';
        write_all(os.str());
    }

    // クエリ回数の制限。count_query() が q 回を超えたら WA。
    void query_limit(long long q) { limit_ = q; }
    void count_query() {
        ++queries_;
        if (limit_ >= 0 && queries_ > limit_)
            wrong("query limit exceeded (" + std::to_string(queries_) + " > " + std::to_string(limit_) + ")");
    }
    long long queries() const { return queries_; }

    void ensure(bool cond, const std::string& msg) {
        if (!cond) wrong(msg);
    }
    [[noreturn]] void wrong(const std::string& msg) { judge_exit(kWrong, "WA: " + msg); }
    [[noreturn]] void accept(const std::string& msg = "") { judge_exit(kAccepted, msg); }
    [[noreturn]] void fail(const std::string& msg) { judge_exit(kJudgeFail, "interactor failure: " + msg); }

  private:
    // atexit の時点では Interactor が破棄されていることがあるので static に置く
    static inline long long queries_ = 0, limit_ = -1;

    // 使ったクエリ回数をドライバに伝える（STRESS_REPORT が無ければ何もしない）
    static void report() {
        const char* path = std::getenv("STRESS_REPORT");
        if (!path || (queries_ == 0 && limit_ < 0)) return;
        if (FILE* f = std::fopen(path, "w")) {
            std::fprintf(f, "queries %lld\nlimit %lld\n", queries_, limit_);
            std::fclose(f);
        }
    }

    template <class T>
    static void put(std::ostringstream& os, const T& x, bool& first) {
        if constexpr (std::is_convertible_v<const T&, std::string> || std::is_arithmetic_v<T>) {
            if (!first) os << ' ';
            first = false;
            if constexpr (std::is_same_v<T, char>)
                os << x;
            else if constexpr (std::is_arithmetic_v<T> && sizeof(T) == 1)
                os << static_cast<int>(x);  // int8_t などを文字として出さない
            else
                os << x;
        } else {
            for (const auto& e : x) put(os, e, first);
        }
    }

    void write_all(const std::string& s) {
        size_t done = 0;
        while (done < s.size()) {
            const ssize_t n = ::write(1, s.data() + done, s.size() - done);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) wrong("main closed its input (it ended before the interaction finished)");
            done += static_cast<size_t>(n);
        }
    }
};

}  // namespace stress

using stress::Interactor;

#ifndef STRESS_NO_MAIN
void interact(stress::Interactor& it);

int main(int argc, char** argv) {
    stress::Interactor it(argc, argv);
    interact(it);
    it.out.expect_eof();  // main が余計な出力をしていないか（main が出力を閉じるまで待つ）
    return stress::kAccepted;
}
#endif
