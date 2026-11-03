#pragma once

// 出力: out / out_lines / raw

#include "core.hpp"
#include "graph.hpp"

namespace stress {

class Output {
  public:

    // 引数を空白区切りで 1 行に出す。vector は要素を空白区切りで展開する。
    template <class... A>
    void out(const A&... args) {
        bool first = true;
        (write_item(args, first), ...);
        buf_ += '\n';
    }
    // 辺のリストは 1 本 1 行（重み付きなら "u v w"）。
    void out(const Edges& es) {
        for (const auto& e : es) {
            if (es.weighted) out(e.u, e.v, e.w);
            else out(e.u, e.v);
        }
    }
    // 1 要素 1 行（グリッドや文字列のリスト向け）。
    template <class T>
    void out_lines(const std::vector<T>& v) {
        for (const auto& x : v) out(x);
    }
    // 改行なしでそのまま足す。
    void raw(std::string_view s) { buf_ += s; }

    const std::string& buffer() const { return buf_; }

  private:
    std::string buf_;

    template <class T>
    void write_item(const T& x, bool& first) {
        if constexpr (std::is_same_v<T, std::vector<std::string>> || std::is_same_v<T, Edges>) {
            static_assert(!sizeof(T), "use out(edges) or out_lines(vector<string>) on its own");
        } else if constexpr (is_vector<T>::value) {
            for (const auto& e : x) write_item(e, first);
        } else {
            if (!first) buf_ += ' ';
            first = false;
            write_scalar(x);
        }
    }
    template <class T>
    struct is_vector : std::false_type {};
    template <class T, class A>
    struct is_vector<std::vector<T, A>> : std::true_type {};

    template <class T>
    void write_scalar(const T& x) {
        if constexpr (std::is_same_v<T, char>) {
            buf_ += x;
        } else if constexpr (std::is_same_v<T, bool>) {
            buf_ += x ? '1' : '0';
        } else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
            char tmp[64];
            auto res = std::to_chars(tmp, tmp + sizeof(tmp), x);
            buf_.append(tmp, res.ptr);
        } else {
            buf_ += std::string_view(x);
        }
    }
};

}  // namespace stress
