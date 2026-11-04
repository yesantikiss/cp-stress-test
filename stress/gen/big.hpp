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
    std::string_view digits = digits::dec;
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

    // [lo, hi] の整数。lo と hi は digits の文字で書くか、10 進なら "1e100" のようにも書ける。
    // 桁数を先に一様に選ぶので、小さい値も大きい値も出る。1/8 で端の値（lo, lo+1, hi-1, hi, b^k, b^k-1）。
    std::string big_range(std::string_view lo_s, std::string_view hi_s, BigRangeOpt opt = {}) {
        const auto d = opt.digits;
        check_digits(d, "big_range");
        const ll b = static_cast<ll>(d.size());
        Num lo = parse(lo_s, d), hi = parse(hi_s, d);
        if (less(hi, lo)) gen_error("big_range: lo > hi");
        if (self().uniform(0, 7) == 0) {
            std::vector<Num> cand{lo, add1(lo, b), sub1(hi, b), hi};
            for (ll len = static_cast<ll>(lo.size()); len <= static_cast<ll>(hi.size()) + 1; ++len) {
                Num p(len, 0);
                p[0] = 1;
                cand.push_back(p);
                cand.push_back(Num(len, b - 1));
            }
            std::vector<Num> ok;
            for (auto& x : cand)
                if (!less(x, lo) && !less(hi, x)) ok.push_back(x);
            return show(self().choice(ok), d);
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

    // 文字列を桁の列に。先頭の 0 は落とす（0 自体は {0}）。
    static Num parse(std::string_view s, std::string_view d) {
        if (s.empty()) gen_error("big_range: empty number");
        Num x;
        const auto e = s.find('e');
        if (e != std::string_view::npos && d.find('e') == std::string_view::npos) {
            if (d != digits::dec) gen_error("big_range: \"" + std::string(s) + "\": e notation needs digits::dec");
            const std::string_view mant = s.substr(0, e), ex = s.substr(e + 1);
            ll k = 0;
            auto res = std::from_chars(ex.data(), ex.data() + ex.size(), k);
            if (mant.empty() || ex.empty() || res.ec != std::errc() || res.ptr != ex.data() + ex.size() || k < 0)
                gen_error("big_range: bad number \"" + std::string(s) + "\"");
            x = parse(mant, d);
            if (!(x.size() == 1 && x[0] == 0)) x.resize(x.size() + k, 0);
            return x;
        }
        for (char ch : s) {
            const auto p = d.find(ch);
            if (p == std::string_view::npos)
                gen_error("big_range: \"" + std::string(s) + "\" has a character not in digits");
            x.push_back(static_cast<ll>(p));
        }
        ll z = 0;
        while (z + 1 < static_cast<ll>(x.size()) && x[z] == 0) ++z;
        x.erase(x.begin(), x.begin() + z);
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
