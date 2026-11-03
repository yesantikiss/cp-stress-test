#pragma once

// checker（judge.hpp）と interactor（interact.hpp）で共有する読み取り部分。
// 直接 include する必要はない。

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <type_traits>
#include <unistd.h>

namespace stress {

enum JudgeExit { kAccepted = 0, kWrong = 1, kJudgeFail = 3 };

[[noreturn]] inline void judge_exit(JudgeExit code, const std::string& msg) {
    if (!msg.empty()) std::cerr << msg << std::endl;
    std::exit(code);
}

// 空白区切りのトークンを読む。読めない・範囲外のとき、
// 解答側 (out) なら WA、入力側 (in / ref) なら checker / interactor の失敗にする。
// ファイルからも（全部読み込む）、パイプからも（必要な分だけ逐次読む）読める。
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

    // fd から逐次読む（interactor が main の出力を読むとき）
    static Reader stream(int fd, std::string name, JudgeExit on_error) {
        Reader r;
        r.name_ = std::move(name);
        r.on_error_ = on_error;
        r.fd_ = fd;
        r.open_ = true;
        return r;
    }

    bool is_open() const { return open_; }

    // 残りが空白だけなら true。パイプのときは相手が閉じるまで待つ。
    bool seek_eof() {
        skip_space();
        return !available();
    }

    std::string read_token() {
        skip_space();
        ++tokens_;  // エラーメッセージの token 番号は、今読んでいる（読もうとした）トークン
        if (!available()) fail("unexpected end of file");
        std::string t;
        while (available() && !is_space(data_[pos_])) t += data_[pos_++];
        return t;
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

    unsigned long long read_u64() {
        const std::string t = read_token();
        if (t.empty() || t.size() > 20) fail("expected an unsigned integer, got \"" + t + "\"");
        for (char ch : t)
            if (ch < '0' || ch > '9') fail("expected an unsigned integer, got \"" + t + "\"");
        errno = 0;
        const unsigned long long v = std::strtoull(t.c_str(), nullptr, 10);
        if (errno == ERANGE) fail("integer out of 64-bit range: " + t);
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
        if (!seek_eof()) {
            ++tokens_;
            fail("extra output after the expected end");
        }
    }

    // cin と同じ感覚で読む。読めなければ（型の範囲外を含む）read_int などと同じく失敗にする。
    // 範囲チェックはしないので、必要なら読んだあとに ensure で確かめる。
    template <class T>
    Reader& operator>>(T& x) {
        if constexpr (std::is_same_v<T, std::string>) {
            x = read_token();
        } else if constexpr (std::is_same_v<T, char>) {
            const std::string t = read_token();
            if (t.size() != 1) fail("expected a single character, got \"" + t + "\"");
            x = t[0];
        } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
            static_assert(sizeof(T) <= sizeof(long long), "use read_token for wider integers");
            if constexpr (std::is_unsigned_v<T> && sizeof(T) == sizeof(long long))
                x = static_cast<T>(read_u64());
            else
                x = static_cast<T>(read_int(static_cast<long long>(std::numeric_limits<T>::min()),
                                            static_cast<long long>(std::numeric_limits<T>::max())));
        } else if constexpr (std::is_floating_point_v<T>) {
            x = static_cast<T>(read_double());
        } else {
            static_assert(sizeof(T) == 0, "Reader >> supports integers, floating point, char and std::string");
        }
        return *this;
    }

    [[noreturn]] void fail(const std::string& msg) const {
        judge_exit(on_error_, name_ + " (token " + std::to_string(tokens_) + "): " + msg);
    }

  private:
    std::string data_, name_;
    size_t pos_ = 0, tokens_ = 0;
    JudgeExit on_error_ = kJudgeFail;
    bool open_ = false;
    int fd_ = -1;  // >= 0 ならパイプから逐次読む
    bool eof_ = false;

    static bool is_space(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

    // pos_ に 1 文字以上あるか。パイプなら届くまで待つ。
    bool available() {
        if (pos_ < data_.size()) return true;
        if (fd_ < 0 || eof_) return false;
        if (pos_ > 0) {
            data_.clear();
            pos_ = 0;
        }
        char buf[1 << 16];
        while (true) {
            const ssize_t n = ::read(fd_, buf, sizeof(buf));
            if (n > 0) {
                data_.append(buf, static_cast<size_t>(n));
                return true;
            }
            if (n < 0 && errno == EINTR) continue;
            eof_ = true;
            return false;
        }
    }

    void skip_space() {
        while (available() && is_space(data_[pos_])) ++pos_;
    }
};

}  // namespace stress
