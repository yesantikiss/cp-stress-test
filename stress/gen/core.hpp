#pragma once

// 乱数の土台: xoshiro256**、[l, r] の乱数、サイズ、値の指定（Range / ラムダ）。

#include <algorithm>
#include <charconv>
#include <cmath>
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

struct DecimalOpt {
    bool exact = false;  // true なら小数点以下ちょうど d 桁、false なら d 桁以下
};

class Core {
  public:
    // true なら tree / parents / graph / digraph / dag / bipartite / perm の番号を 1 始まり、false なら 0 始まりにする。
    // 関数を呼んだときに読むので、途中で代入するとそれ以降の呼び出しだけが変わる。
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

    // [lo, hi] の小数を文字列で返す。既定は小数点以下 d 桁以下（末尾の 0 は付けない）で、
    // 先に桁数 k を 0..d から選ぶので "3" "3.5" "3.14" のように桁数がばらける。
    // exact なら小数点以下ちょうど d 桁（"3.000"）。値は 10^-d 刻みで、端が出やすいのは i と同じ。
    std::string decimal(double lo, double hi, ll d, DecimalOpt opt = {}) {
        if (d < 0 || d > 18) gen_error("decimal: d must be in [0, 18]");
        ll scale = 1;
        for (ll t = 0; t < d; ++t) scale *= 10;
        const long double sl = static_cast<long double>(lo) * scale, sh = static_cast<long double>(hi) * scale;
        constexpr long double lim = 9.0e18L;
        if (!(-lim < sl && sl < lim && -lim < sh && sh < lim))
            gen_error("decimal: |lo|, |hi| times 10^d must be below 9e18");
        const ll L = round_scaled(sl, true), H = round_scaled(sh, false);
        if (L > H) gen_error("decimal: no value with " + std::to_string(d) + " decimals in [lo, hi]");
        ll v;
        if (opt.exact) {
            v = i(L, H);
        } else {
            // 桁数 k を選ぶ。その刻みの値が範囲に無ければ k を増やす（k = d なら必ずある）。
            ll k = uniform(0, d), step = 1;
            for (ll t = k; t < d; ++t) step *= 10;
            while (floor_div(H, step) < ceil_div(L, step)) step /= 10;
            v = i(ceil_div(L, step), floor_div(H, step)) * step;
        }
        const ll ip = v / scale, fp = v % scale;  // 0 に向かって切り捨てるので符号は同じ
        std::string res = (v < 0 ? "-" : "") + std::to_string(ip < 0 ? -ip : ip);
        if (d > 0) {
            std::string frac = std::to_string(fp < 0 ? -fp : fp);
            frac.insert(0, static_cast<std::size_t>(d) - frac.size(), '0');
            if (!opt.exact)
                while (!frac.empty() && frac.back() == '0') frac.pop_back();
            if (!frac.empty()) res += "." + frac;
        }
        return res;
    }

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

    // ---------- タグ（失敗したときに st が表示する） ----------

    // このケースの種類を記録する。mixed で選ばれた形は自動で付く（"str=palindrome" など）。
    // 同じタグは回数を数えるだけで、何度付けてもいい。
    void tag(std::string_view t) {
        for (auto& [name, count] : tags_)
            if (name == t) {
                ++count;
                return;
            }
        tags_.push_back({std::string(t), 1});
    }
    const std::vector<std::pair<std::string, ll>>& tags() const { return tags_; }

  private:
    static ll floor_div(ll a, ll b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }
    static ll ceil_div(ll a, ll b) { return -floor_div(-a, b); }
    // x * 10^d を整数にする。誤差で 2.9999999 になった 3 は 3 として扱う。
    static ll round_scaled(long double x, bool up) {
        const long double r = std::round(x);
        if (std::fabs(x - r) < 1e-6L) return static_cast<ll>(r);
        return static_cast<ll>(up ? std::ceil(x) : std::floor(x));
    }
    std::uint64_t s_[4];
    ll cap_;
    std::vector<std::pair<std::string, ll>> tags_;  // 付けた順、(タグ, 回数)
    static std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    static std::uint64_t splitmix64(std::uint64_t& x) {
        std::uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
};

}  // namespace stress
