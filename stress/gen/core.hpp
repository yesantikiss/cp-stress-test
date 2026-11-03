#pragma once

// 乱数の土台: xoshiro256**、[l, r] の乱数、サイズ、値の指定（Range / ラムダ）。

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace stress {

using ll = long long;

[[noreturn]] inline void gen_error(const std::string& msg) {
    std::cerr << "[gen error] " << msg << std::endl;
    std::exit(3);
}

// 閉区間 [lo, hi]。値を 1 個返すラムダの省略形として、どこでも使える。
struct Range {
    ll lo, hi;
};

// 重みなどの値の指定。無し / Range / 値を返すラムダ。
using Value = std::variant<std::monostate, Range, std::function<ll()>>;

class Core {
  public:
    // true なら tree / graph / perm の頂点番号を 1 始まりにする。
    bool one_indexed = true;

    explicit Core(std::uint64_t seed, ll cap = std::numeric_limits<ll>::max())
        : cap_(cap) {
        std::uint64_t x = seed;
        for (auto& s : s_) s = splitmix64(x);
    }

    ll cap() const { return cap_; }
    ll base() const { return one_indexed ? 1 : 0; }

    // ---------- 乱数の基本 ----------

    std::uint64_t next() {  // xoshiro256**
        const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const std::uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    // [lo, hi] の一様乱数。
    ll uniform(ll lo, ll hi) {
        if (lo > hi) gen_error("uniform(" + std::to_string(lo) + ", " + std::to_string(hi) + "): lo > hi");
        const std::uint64_t span = static_cast<std::uint64_t>(hi) - static_cast<std::uint64_t>(lo) + 1;
        if (span == 0) return static_cast<ll>(next());  // 64bit 全域
        // Lemire の方法（偏りなし）
        unsigned __int128 m = static_cast<unsigned __int128>(next()) * span;
        std::uint64_t low = static_cast<std::uint64_t>(m);
        if (low < span) {
            const std::uint64_t threshold = -span % span;
            while (low < threshold) {
                m = static_cast<unsigned __int128>(next()) * span;
                low = static_cast<std::uint64_t>(m);
            }
        }
        return static_cast<ll>(static_cast<std::uint64_t>(lo) + static_cast<std::uint64_t>(m >> 64));
    }

    // [lo, hi] の乱数。1/8 の確率で端 (lo, lo+1, hi-1, hi) を返す。
    ll i(ll lo, ll hi) {
        if (lo > hi) gen_error("i(" + std::to_string(lo) + ", " + std::to_string(hi) + "): lo > hi");
        if (lo < hi && uniform(0, 7) == 0) {
            switch (uniform(0, 3)) {
                case 0: return lo;
                case 1: return lo + 1;
                case 2: return hi - 1;
                default: return hi;
            }
        }
        return uniform(lo, hi);
    }
    ll i(Range r) { return i(r.lo, r.hi); }

    // サイズ用。hi をドライバの cap で切り詰める（ただし lo は下回らない）。
    ll size(ll lo, ll hi) {
        const ll h = std::max(lo, std::min(hi, cap_));
        return i(lo, h);
    }

    double real(double lo, double hi) {
        const double u = static_cast<double>(next() >> 11) * 0x1.0p-53;  // [0, 1)
        return lo + (hi - lo) * u;
    }

    bool coin(double p = 0.5) { return real(0.0, 1.0) < p; }

    // ---------- 選択と並べ替え ----------

    template <class T>
    const T& choice(const std::vector<T>& v) {
        if (v.empty()) gen_error("choice: empty vector");
        return v[uniform(0, static_cast<ll>(v.size()) - 1)];
    }
    char choice(std::string_view s) {
        if (s.empty()) gen_error("choice: empty string");
        return s[uniform(0, static_cast<ll>(s.size()) - 1)];
    }

    template <class T>
    void shuffle(std::vector<T>& v) {
        for (ll k = static_cast<ll>(v.size()) - 1; k > 0; --k) std::swap(v[k], v[uniform(0, k)]);
    }
    void shuffle(std::string& s) {
        for (ll k = static_cast<ll>(s.size()) - 1; k > 0; --k) std::swap(s[k], s[uniform(0, k)]);
    }

    // ---------- 値の指定（Range / ラムダ）を 1 個引く ----------

    template <class V>
    ll draw(V&& v) {
        using D = std::decay_t<V>;
        if constexpr (std::is_same_v<D, Range>) {
            return i(v.lo, v.hi);
        } else if constexpr (std::is_same_v<D, Value>) {
            if (std::holds_alternative<Range>(v)) return i(std::get<Range>(v));
            if (std::holds_alternative<std::function<ll()>>(v)) return std::get<std::function<ll()>>(v)();
            return 0;
        } else {
            return static_cast<ll>(v());
        }
    }

  private:
    std::uint64_t s_[4];
    ll cap_;
    static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    static std::uint64_t splitmix64(std::uint64_t& x) {
        std::uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
};

}  // namespace stress
