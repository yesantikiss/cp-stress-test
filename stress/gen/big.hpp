#pragma once

// 巨大整数（文字列で返す）: big / big_range、数字の並び digits::*

#include "core.hpp"

namespace stress {

// 数字の並び。長さが進数になり、k 番目の文字が値 k を表す。
namespace digits {
inline constexpr std::string_view bin = "01";
inline constexpr std::string_view oct = "01234567";
inline constexpr std::string_view dec = "0123456789";
inline constexpr std::string_view hex = "0123456789abcdef";
inline constexpr std::string_view HEX = "0123456789ABCDEF";
inline constexpr std::string_view base36 = "0123456789abcdefghijklmnopqrstuvwxyz";
}  // namespace digits

struct BigOpt {
    std::string_view digits = digits::dec;
    bool lead_zero = false;  // true なら先頭が 0 でもいい
};

struct BigRangeOpt {
    std::string_view digits = digits::dec;  // 出力の進数。範囲 lo, hi の書き方には関係ない
};

template <class G>
class BigOps {
    G& self() { return static_cast<G&>(*this); }
    using Num = std::vector<ll>;  // 上の桁から順に各桁の値

  public:

    // ちょうど len 桁の整数。lead_zero = false なら先頭は 0 以外（len = 1 なら "0" も出る）。
    std::string big(ll len, BigOpt opt = {}) {
        check_digits(opt.digits, "big");
        if (len <= 0) gen_error("big: len must be positive");
        const ll b = static_cast<ll>(opt.digits.size());
        std::string s(len, ' ');
        for (ll k = 0; k < len; ++k) {
            const ll lo = (k == 0 && len > 1 && !opt.lead_zero) ? 1 : 0;
            s[k] = opt.digits[self().uniform(lo, b - 1)];
        }
        return s;
    }

    // [lo, hi] の整数を digits の進数で返す。lo と hi は進数によらず次の形で書く:
    //   "123"（10 進）、"1e18"（A×10^K）、"2^60"（B^K）、それぞれに "-1" を付けたもの（"2^60-1"）。
    // 桁数を先に一様に選ぶので、小さい値も大きい値も出る。1/8 で端の値（lo, lo+1, hi-1, hi, b^k, b^k-1）。
    std::string big_range(std::string_view lo_s, std::string_view hi_s, BigRangeOpt opt = {}) {
        const auto d = opt.digits;
        check_digits(d, "big_range");
        const ll b = static_cast<ll>(d.size());
        Num lo = parse(lo_s, b), hi = parse(hi_s, b);
        if (less(hi, lo)) gen_error("big_range: lo > hi");
        if (self().uniform(0, 7) == 0) {
            for (int attempt = 0; attempt < 8; ++attempt) {
                Num x;
                switch (self().uniform(0, 5)) {
                    case 0: x = lo; break;
                    case 1: x = add1(lo, b); break;
                    case 2: x = sub1(hi, b); break;
                    case 3: x = hi; break;
                    default: {  // b^k または b^k - 1
                        const ll k = self().uniform(std::max<ll>(0, static_cast<ll>(lo.size()) - 1),
                                                    static_cast<ll>(hi.size()));
                        x = power_of_base(k);
                        if (self().coin()) x = sub1(x, b);
                    }
                }
                if (!less(x, lo) && !less(hi, x)) return show(x, d);
            }
            return show(self().coin() ? lo : hi, d);
        }
        const ll len = self().uniform(static_cast<ll>(lo.size()), static_cast<ll>(hi.size()));
        // この桁数での範囲 [a, c] に絞る
        Num a = lo, c = hi;
        if (static_cast<ll>(a.size()) < len) {
            a.assign(len, 0);
            a[0] = len > 1 ? 1 : 0;
        }
        if (static_cast<ll>(c.size()) > len) c.assign(len, b - 1);
        // 上の桁から、a と c に挟まれる範囲で一様に選ぶ（値として厳密な一様ではない）。
        Num x(len);
        bool tight_lo = true, tight_hi = true;
        for (ll k = 0; k < len; ++k) {
            const ll l = tight_lo ? a[k] : 0, r = tight_hi ? c[k] : b - 1;
            x[k] = self().uniform(l, r);
            tight_lo = tight_lo && x[k] == a[k];
            tight_hi = tight_hi && x[k] == c[k];
        }
        return show(x, d);
    }

  private:
    static void check_digits(std::string_view d, const char* who) {
        if (d.size() < 2) gen_error(std::string(who) + ": digits must have at least 2 characters");
    }

    static Num power_of_base(ll k) {  // b^k
        Num x(k + 1, 0);
        x[0] = 1;
        return x;
    }

    [[noreturn]] static void bad_number(std::string_view s) {
        gen_error("big_range: bad number \"" + std::string(s) +
                  "\" (write 123, 1e18, 2^60, or one of them followed by -1)");
    }
    static ll parse_small(std::string_view s, std::string_view whole) {
        ll v = 0;
        auto res = std::from_chars(s.data(), s.data() + s.size(), v);
        if (s.empty() || res.ec != std::errc() || res.ptr != s.data() + s.size() || v < 0) bad_number(whole);
        return v;
    }
    static std::string_view check_decimal(std::string_view s, std::string_view whole) {
        if (s.empty()) bad_number(whole);
        for (char ch : s)
            if (ch < '0' || ch > '9') bad_number(whole);
        return s;
    }

    // 範囲の文字列を b 進の桁の列にする。先頭の 0 は落とす（0 自体は {0}）。
    static Num parse(std::string_view s, ll b) {
        const std::string_view whole = s;
        bool minus_one = false;
        if (s.size() > 2 && s.substr(s.size() - 2) == "-1") {
            minus_one = true;
            s.remove_suffix(2);
        }
        Num x;
        if (const auto p = s.find('^'); p != std::string_view::npos) {
            const ll base = parse_small(s.substr(0, p), whole), k = parse_small(s.substr(p + 1), whole);
            if (base < 2) bad_number(whole);
            if (base == b) {
                x = power_of_base(k);  // O(k)
            } else {
                x = {1};
                mul_pow(x, b, base, k);
            }
        } else if (const auto e = s.find('e'); e != std::string_view::npos) {
            const std::string_view mant = check_decimal(s.substr(0, e), whole);
            const ll k = parse_small(s.substr(e + 1), whole);
            x = from_decimal(mant, b);
            if (!is_zero(x)) {
                if (b == 10) x.resize(x.size() + k, 0);
                else mul_pow(x, b, 10, k);
            }
        } else {
            x = from_decimal(check_decimal(s, whole), b);
        }
        if (minus_one) {
            if (is_zero(x)) bad_number(whole);
            x = sub1(x, b);
        }
        return x;
    }
    static bool is_zero(const Num& x) { return x.size() == 1 && x[0] == 0; }
    // x = x * m + c（x は b 進）
    static void mul_add(Num& x, ll b, ll m, ll c) {
        __int128 carry = c;
        for (ll k = static_cast<ll>(x.size()) - 1; k >= 0; --k) {
            const __int128 v = static_cast<__int128>(x[k]) * m + carry;
            x[k] = static_cast<ll>(v % b);
            carry = v / b;
        }
        Num head;
        while (carry > 0) head.push_back(static_cast<ll>(carry % b)), carry /= b;
        x.insert(x.begin(), head.rbegin(), head.rend());
        if (x.size() > 1 && x[0] == 0) {
            ll z = 0;
            while (z + 1 < static_cast<ll>(x.size()) && x[z] == 0) ++z;
            x.erase(x.begin(), x.begin() + z);
        }
    }
    // x *= base^k。base^t <= 10^18 になる t 個ずつまとめて掛ける。
    static void mul_pow(Num& x, ll b, ll base, ll k) {
        ll t = 1, m = base;
        while (m <= 1000000000000000000LL / base) m *= base, ++t;
        for (; k >= t; k -= t) mul_add(x, b, m, 0);
        for (; k > 0; --k) mul_add(x, b, base, 0);
    }
    // 10 進の文字列を b 進に。b = 10 なら O(桁数)、それ以外は 18 桁ずつ掛けて足すので O(桁数^2 / 18)。
    static Num from_decimal(std::string_view s, ll b) {
        Num x;
        if (b == 10) {
            for (char ch : s) x.push_back(ch - '0');
            ll z = 0;
            while (z + 1 < static_cast<ll>(x.size()) && x[z] == 0) ++z;
            x.erase(x.begin(), x.begin() + z);
            return x;
        }
        x = {0};
        size_t pos = 0;
        const size_t first = s.size() % 18 == 0 ? 18 : s.size() % 18;
        while (pos < s.size()) {
            const size_t len = pos == 0 ? first : 18;
            ll chunk = 0, m = 1;
            for (size_t k = pos; k < pos + len; ++k) chunk = chunk * 10 + (s[k] - '0'), m *= 10;
            mul_add(x, b, m, chunk);
            pos += len;
        }
        return x;
    }
    static bool less(const Num& x, const Num& y) {
        if (x.size() != y.size()) return x.size() < y.size();
        return x < y;
    }
    static Num add1(Num x, ll b) {
        for (ll k = static_cast<ll>(x.size()) - 1; k >= 0; --k) {
            if (++x[k] < b) return x;
            x[k] = 0;
        }
        x.insert(x.begin(), 1);
        return x;
    }
    static Num sub1(Num x, ll b) {  // 0 - 1 は 0 のまま返す
        if (x.size() == 1 && x[0] == 0) return x;
        for (ll k = static_cast<ll>(x.size()) - 1; k >= 0; --k) {
            if (x[k]-- > 0) break;
            x[k] = b - 1;
        }
        if (x.size() > 1 && x[0] == 0) x.erase(x.begin());
        return x;
    }
    static std::string show(const Num& x, std::string_view d) {
        std::string s(x.size(), ' ');
        for (size_t k = 0; k < x.size(); ++k) s[k] = d[x[k]];
        return s;
    }
};

}  // namespace stress
