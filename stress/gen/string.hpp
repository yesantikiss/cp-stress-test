#pragma once

// 文字列: str / mutate / strs / grid

#include "core.hpp"

namespace stress {

enum class StrShape { mixed, random, periodic, palindrome, fibonacci, runs };

struct StrOpt {
    StrShape shape = StrShape::mixed;
};

template <class G>
class StringOps {
    G& self() { return static_cast<G&>(*this); }

  public:

    std::string str(ll n, std::string_view alphabet, StrOpt opt = {}) {
        if (alphabet.empty()) gen_error("str: empty alphabet");
        if (n < 0) gen_error("str: n must be >= 0");
        if (n == 0) return "";
        StrShape shape = opt.shape;
        if (shape == StrShape::mixed) {
            static constexpr StrShape all[] = {StrShape::random, StrShape::random, StrShape::periodic,
                                               StrShape::palindrome, StrShape::fibonacci, StrShape::runs};
            shape = all[self().uniform(0, std::size(all) - 1)];
            self().tag(std::string("str=") + shape_name(shape));
        }
        switch (shape) {
            case StrShape::periodic: return periodic_str(n, alphabet);
            case StrShape::palindrome: return palindrome_str(n, alphabet);
            case StrShape::fibonacci: return fibonacci_str(n, alphabet);
            case StrShape::runs: return runs_str(n, alphabet);
            default: return random_str(n, alphabet);
        }
    }
    template <class F, std::enable_if_t<std::is_invocable_v<F&>, int> = 0>
    std::string str(ll n, F&& f) {
        if (n < 0) gen_error("str: n must be >= 0");
        std::string s(n, ' ');
        for (auto& c : s) c = static_cast<char>(f());
        return s;
    }

    // s のうち k 文字を書き換える。alphabet 省略時は s に現れる文字から選ぶ。
    std::string mutate(std::string s, ll k, std::string_view alphabet = {}) {
        if (k < 0) gen_error("mutate: k must be >= 0");
        std::string chars(alphabet);
        if (chars.empty()) {
            chars = s;
            std::sort(chars.begin(), chars.end());
            chars.erase(std::unique(chars.begin(), chars.end()), chars.end());
        }
        if (s.empty() || chars.empty()) return s;
        for (ll t = 0; t < k; ++t) s[self().uniform(0, static_cast<ll>(s.size()) - 1)] = self().choice(chars);
        return s;
    }

    // k 個の文字列（各長さ 1 以上、長さの総和は total 以下）。
    std::vector<std::string> strs(ll k, ll total, std::string_view alphabet, StrOpt opt = {}) {
        if (alphabet.empty()) gen_error("strs: empty alphabet");
        if (k < 0) gen_error("strs: k must be >= 0");
        if (total < k) gen_error("strs: total must be >= k");
        if (k == 0) return {};
        const ll sum = self().i(k, total);
        auto lens = self().partition(sum, k, 1);
        std::vector<std::string> res;
        res.reserve(k);
        for (ll len : lens) res.push_back(str(len, alphabet, opt));
        return res;
    }

    std::vector<std::string> grid(ll h, ll w, std::string_view chars) {
        if (h < 0 || w < 0) gen_error("grid: h and w must be >= 0");
        if (chars.empty()) gen_error("grid: empty chars");
        std::vector<std::string> g(h, std::string(w, ' '));
        for (auto& row : g)
            for (auto& c : row) c = self().choice(chars);
        return g;
    }
    template <class F, std::enable_if_t<std::is_invocable_v<F&>, int> = 0>
    std::vector<std::string> grid(ll h, ll w, F&& f) {
        if (h < 0 || w < 0) gen_error("grid: h and w must be >= 0");
        std::vector<std::string> g(h, std::string(w, ' '));
        for (auto& row : g)
            for (auto& c : row) c = static_cast<char>(f());
        return g;
    }

  private:
    static bool has_two_distinct(std::string_view a) {
        for (std::size_t i = 1; i < a.size(); ++i)
            if (a[i] != a[0]) return true;
        return false;
    }
    static const char* shape_name(StrShape s) {
        switch (s) {
            case StrShape::random: return "random";
            case StrShape::periodic: return "periodic";
            case StrShape::palindrome: return "palindrome";
            case StrShape::fibonacci: return "fibonacci";
            case StrShape::runs: return "runs";
            default: return "mixed";
        }
    }
    std::string random_str(ll n, std::string_view a) {
        std::string s(n, ' ');
        for (auto& c : s) c = self().choice(a);
        return s;
    }
    std::string periodic_str(ll n, std::string_view a) {
        const ll p = self().uniform(1, std::max<ll>(1, self().coin() ? std::min<ll>(n, 4) : n / 2));
        const std::string unit = random_str(p, a);
        std::string s(n, ' ');
        for (ll k = 0; k < n; ++k) s[k] = unit[k % p];
        return mutate(s, self().uniform(0, 2), a);
    }
    std::string palindrome_str(ll n, std::string_view a) {
        std::string s;
        const ll pieces = self().coin() ? 1 : self().uniform(2, 4);
        auto lens = self().partition(n, std::min(pieces, n), 1);
        for (ll len : lens) {
            std::string half = random_str((len + 1) / 2, a);
            std::string piece = half;
            piece.append(half.rbegin() + (len % 2), half.rend());
            s += piece;
        }
        return s;
    }
    std::string fibonacci_str(ll n, std::string_view a) {
        if (!has_two_distinct(a)) return std::string(n, a[0]);
        char x = self().choice(a), y = self().choice(a);
        while (y == x) y = self().choice(a);
        std::string s0(1, x), s1{x, y};
        const ll want = n + self().uniform(0, n);
        while (static_cast<ll>(s1.size()) < want) {
            std::string s2 = s1 + s0;
            s0 = std::move(s1);
            s1 = std::move(s2);
        }
        const ll start = self().uniform(0, static_cast<ll>(s1.size()) - n);
        return s1.substr(start, n);
    }
    std::string runs_str(ll n, std::string_view a) {
        if (!has_two_distinct(a)) return std::string(n, a[0]);
        std::string s;
        const ll max_run = std::max<ll>(1, self().coin() ? n : (n + 3) / 4);
        char prev = 0;
        bool has_prev = false;
        while (static_cast<ll>(s.size()) < n) {
            char c = self().choice(a);
            if (has_prev) while (c == prev) c = self().choice(a);
            const ll len = std::min<ll>(self().uniform(1, max_run), n - static_cast<ll>(s.size()));
            s.append(len, c);
            prev = c;
            has_prev = true;
        }
        return s;
    }
};

}  // namespace stress
