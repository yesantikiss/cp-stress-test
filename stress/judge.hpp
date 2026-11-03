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

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

namespace stress {

enum JudgeExit { kAccepted = 0, kWrong = 1, kJudgeFail = 3 };

[[noreturn]] inline void judge_exit(JudgeExit code, const std::string& msg) {
    if (!msg.empty()) std::cerr << msg << std::endl;
    std::exit(code);
}

// 空白区切りのトークンを読む。読めない・範囲外のとき、
// 解答側 (out) なら WA、入力側 (in / ref) なら checker の失敗にする。
class Reader {
  public:
    Reader() = default;
    Reader(const std::string& path, std::string name, JudgeExit on_error)
        : name_(std::move(name)), on_error_(on_error) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) judge_exit(kJudgeFail, "cannot open " + name_ + ": " + path);
        data_.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
        open_ = true;
    }

    bool is_open() const { return open_; }

    bool seek_eof() {
        skip_space();
        return pos_ >= data_.size();
    }

    std::string read_token() {
        skip_space();
        if (pos_ >= data_.size()) fail("unexpected end of file");
        const size_t begin = pos_;
        while (pos_ < data_.size() && !is_space(data_[pos_])) ++pos_;
        ++tokens_;
        return data_.substr(begin, pos_ - begin);
    }

    long long read_int(long long lo = std::numeric_limits<long long>::min(),
                       long long hi = std::numeric_limits<long long>::max()) {
        const std::string t = read_token();
        size_t k = (t[0] == '-' || t[0] == '+') ? 1 : 0;
        if (k == t.size()) fail("expected an integer, got \"" + t + "\"");
        for (size_t q = k; q < t.size(); ++q)
            if (t[q] < '0' || t[q] > '9') fail("expected an integer, got \"" + t + "\"");
        errno = 0;
        char* end = nullptr;
        const long long v = std::strtoll(t.c_str(), &end, 10);
        if (errno == ERANGE) fail("integer out of 64-bit range: " + t);
        if (v < lo || v > hi)
            fail("integer " + t + " is out of range [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
        return v;
    }

    double read_double(double lo = -std::numeric_limits<double>::infinity(),
                       double hi = std::numeric_limits<double>::infinity()) {
        const std::string t = read_token();
        char* end = nullptr;
        const double v = std::strtod(t.c_str(), &end);
        if (end != t.c_str() + t.size() || !std::isfinite(v)) fail("expected a real number, got \"" + t + "\"");
        if (v < lo || v > hi) fail("real number " + t + " is out of range");
        return v;
    }

    // 期待した語（"Yes" など）かどうか。大文字小文字は区別する。
    std::string read_word(const std::string& expected = "") {
        const std::string t = read_token();
        if (!expected.empty() && t != expected) fail("expected \"" + expected + "\", got \"" + t + "\"");
        return t;
    }

    void expect_eof() {
        if (!seek_eof()) fail("extra output after the expected end");
    }

    [[noreturn]] void fail(const std::string& msg) const {
        judge_exit(on_error_, name_ + " (token " + std::to_string(tokens_ + 1) + "): " + msg);
    }

  private:
    std::string data_, name_;
    size_t pos_ = 0, tokens_ = 0;
    JudgeExit on_error_ = kJudgeFail;
    bool open_ = false;

    static bool is_space(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
    void skip_space() {
        while (pos_ < data_.size() && is_space(data_[pos_])) ++pos_;
    }
};

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
