#pragma once

// 整数論の値: prime / highly_composite

#include "core.hpp"

namespace stress {

namespace detail {

inline std::uint64_t mul_mod(std::uint64_t a, std::uint64_t b, std::uint64_t m) {
    return static_cast<std::uint64_t>(static_cast<unsigned __int128>(a) * b % m);
}

inline std::uint64_t pow_mod(std::uint64_t a, std::uint64_t e, std::uint64_t m) {
    std::uint64_t r = 1 % m;
    a %= m;
    for (; e; e >>= 1, a = mul_mod(a, a, m))
        if (e & 1) r = mul_mod(r, a, m);
    return r;
}

// 64bit で決定的なミラーラビン。
inline bool is_prime(ll n) {
    if (n < 2) return false;
    for (ll p : {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37})
        if (n % p == 0) return n == p;
    const std::uint64_t u = static_cast<std::uint64_t>(n);
    std::uint64_t d = u - 1;
    int s = 0;
    while ((d & 1) == 0) d >>= 1, ++s;
    for (std::uint64_t a : {2ULL, 325ULL, 9375ULL, 28178ULL, 450775ULL, 9780504ULL, 1795265022ULL}) {
        a %= u;
        if (a == 0) continue;
        std::uint64_t x = pow_mod(a, d, u);
        if (x == 1 || x == u - 1) continue;
        bool comp = true;
        for (int r = 1; r < s && comp; ++r) {
            x = mul_mod(x, x, u);
            if (x == u - 1) comp = false;
        }
        if (comp) return false;
    }
    return true;
}

// long long に収まる高度合成数を小さい順に全部。初回だけ計算する。
inline const std::vector<ll>& all_highly_composite() {
    static const std::vector<ll> res = [] {
        // 高度合成数は 2^a 3^b 5^c ... (a >= b >= c >= ...) の形なので、その形を全部作って約数の個数を比べる。
        constexpr ll primes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53};
        constexpr ll lim = std::numeric_limits<ll>::max();
        std::vector<std::pair<ll, ll>> cand;  // (値, 約数の個数)
        auto dfs = [&](auto&& self, std::size_t k, ll v, ll d, ll max_e) -> void {
            cand.push_back({v, d});
            if (k == std::size(primes)) return;
            ll x = v;
            for (ll e = 1; e <= max_e; ++e) {
                if (x > lim / primes[k]) break;
                x *= primes[k];
                self(self, k + 1, x, d * (e + 1), e);
            }
        };
        dfs(dfs, 0, 1, 1, 64);
        std::sort(cand.begin(), cand.end());
        std::vector<ll> out;
        ll best = 0;
        for (auto [v, d] : cand)
            if (d > best) best = d, out.push_back(v);
        return out;
    }();
    return res;
}

}  // namespace detail

template <class G>
class NumberOps {
    G& self() { return static_cast<G&>(*this); }

  public:
    // [lo, hi] の素数。1/8 の確率で範囲内の最小か最大の素数を返す。
    // それ以外は x を一様に選び、x 以上で最初の素数（無ければ x 以下で最後の素数）を返す。
    ll prime(ll lo, ll hi) {
        const std::string where = "prime(" + std::to_string(lo) + ", " + std::to_string(hi) + ")";
        if (lo > hi) gen_error(where + ": lo > hi");
        lo = std::max<ll>(lo, 2);
        if (lo > hi) gen_error(where + ": no prime in range");
        if (self().uniform(0, 7) == 0) {
            const ll p = self().coin() ? next_prime(lo, hi) : prev_prime(hi, lo);
            if (p < 0) gen_error(where + ": no prime in range");
            return p;
        }
        const ll x = self().uniform(lo, hi);
        ll p = next_prime(x, hi);
        if (p < 0) p = prev_prime(x, lo);
        if (p < 0) gen_error(where + ": no prime in range");
        return p;
    }

    // hi 以下の高度合成数を小さい順に全部。
    std::vector<ll> highly_composite(ll hi) { return highly_composite(1, hi); }

    // [lo, hi] の高度合成数を小さい順に全部。1 個も無ければエラー。
    std::vector<ll> highly_composite(ll lo, ll hi) {
        const std::string where = "highly_composite(" + std::to_string(lo) + ", " + std::to_string(hi) + ")";
        if (lo > hi) gen_error(where + ": lo > hi");
        const auto& all = detail::all_highly_composite();
        std::vector<ll> res(std::lower_bound(all.begin(), all.end(), lo), std::upper_bound(all.begin(), all.end(), hi));
        if (res.empty()) gen_error(where + ": no highly composite number in range");
        return res;
    }

  private:
    // x 以上 hi 以下で最小の素数。無ければ -1。
    static ll next_prime(ll x, ll hi) {
        for (ll v = x;; ++v) {
            if (detail::is_prime(v)) return v;
            if (v == hi) return -1;
        }
    }
    // x 以下 lo 以上で最大の素数。無ければ -1。
    static ll prev_prime(ll x, ll lo) {
        for (ll v = x;; --v) {
            if (detail::is_prime(v)) return v;
            if (v == lo) return -1;
        }
    }
};

}  // namespace stress
